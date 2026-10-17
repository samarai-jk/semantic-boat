#include "slstm32/drivers/ct1780.hpp"

namespace slstm32::drivers {

namespace {

constexpr std::uint8_t commandConvert = 0x44u;
constexpr std::uint8_t commandReadScratchpad = 0xBEu;
// The MAX31850 needs up to 100 ms; DFRobot's library waits 750 ms for the
// CT1780. That is the wait without completion polling and the timeout with it.
constexpr std::uint32_t conversionMs = 750u;
constexpr std::uint32_t pollMs = 1u;
// A conversion reported done this early means the converter does not drive
// the read slots: the released line just reads 1.
constexpr std::uint32_t implausiblyFastMs = 5u;
constexpr std::uint8_t faultOpen = 0x01u;
constexpr std::uint8_t faultShortGround = 0x02u;
constexpr std::uint8_t faultShortSupply = 0x04u;

const char* faultText(std::uint8_t bits) {
    if (bits & faultOpen) return "thermocouple open";
    if (bits & faultShortGround) return "thermocouple shorted to GND";
    if (bits & faultShortSupply) return "thermocouple shorted to VDD";
    return "fault";
}

} // namespace

bool Ct1780Driver::init() {
    if (!runtime_.millis || !bus_.init()) return false;
    cycleAt_ = nextAt_ = runtime_.millis();
    return true;
}

void Ct1780Driver::failed(sensors::Fault fault, std::uint32_t now) {
    ++errors_;
    state_ = State::idle;
    if (thermocouple_) thermocouple_->fail(fault, now);
    if (coldJunction_ && fault != sensors::Fault::sensorReported) coldJunction_->fail(fault, now);
}

void Ct1780Driver::lost() {
    if (rom_ == 0u) return;
    char text[20];
    log_.writef(LogLevel::warning, "ct1780 %s: lost", OneWireBus::formatRom(text, rom_));
    rom_ = 0u;
}

bool Ct1780Driver::readScratchpad(std::uint8_t (&data)[9]) {
    if (!bus_.select(rom_)) return false;
    bus_.writeByte(commandReadScratchpad);
    bus_.read(data, sizeof data);
    // The upper configuration bits read 1; this rejects an all-zero frame,
    // which passes the CRC.
    return OneWireBus::crc8(data, 8u) == data[8] && (data[4] & 0xF0u) == 0xF0u;
}

bool Ct1780Driver::discover(std::uint32_t now) {
    OneWireBus::Rom rom = 0u;
    if (!bus_.readRom(rom)) {
        failed(sensors::Fault::noResponse, now);
        return false;
    }
    char text[20];
    if (OneWireBus::family(rom) != family) {
        if (rom != foreignRom_) {
            log_.writef(LogLevel::warning, "ct1780: %s on the bus is not a CT1780", OneWireBus::formatRom(text, rom));
            foreignRom_ = rom;
        }
        failed(sensors::Fault::noResponse, now);
        return false;
    }
    rom_ = rom;
    if (thermocouple_) thermocouple_->setIdentity(sensors::Identity::oneWire(rom, 0u));
    if (coldJunction_) coldJunction_->setIdentity(sensors::Identity::oneWire(rom, 1u));
    std::uint8_t data[9]{};
    const bool read = readScratchpad(data);
    if (read) address_ = data[4] & 0x0Fu;
    // The raw scratchpad documents the converter on the bench.
    log_.writef(LogLevel::info,
                "ct1780 %s: found, address %u, scratchpad %02X %02X %02X %02X %02X %02X %02X %02X %02X%s",
                OneWireBus::formatRom(text, rom), static_cast<unsigned>(address_), data[0], data[1], data[2],
                data[3], data[4], data[5], data[6], data[7], data[8], read ? "" : " (invalid)");
    return true;
}

void Ct1780Driver::finish(std::uint32_t now) {
    state_ = State::idle;
    std::uint8_t data[9];
    if (!readScratchpad(data)) {
        const bool present = bus_.reset();
        if (!present) lost();
        rom_ = 0u; // look for it again: a swapped converter ignores the old ROM code
        failed(present ? sensors::Fault::checksum : sensors::Fault::noResponse, now);
        return;
    }
    const auto coldRaw = static_cast<std::int16_t>(static_cast<std::uint16_t>(data[3] << 8u | data[2]));
    const std::int32_t coldMilliCelsius = static_cast<std::int32_t>(coldRaw >> 4) * 625 / 10; // 12 bit, 1/16 C
    coldJunctionMilliCelsius_ = coldMilliCelsius;
    if (coldJunction_) coldJunction_->update(coldMilliCelsius, now);

    const auto faults = static_cast<std::uint8_t>((data[0] & 0x01u) ? (data[2] & 0x07u) | 0x80u : 0u);
    if (faults != faultBits_) {
        char text[20];
        if (faults) {
            log_.writef(LogLevel::warning, "ct1780 %s: %s", OneWireBus::formatRom(text, rom_), faultText(faults));
        } else {
            log_.writef(LogLevel::info, "ct1780 %s: thermocouple ok", OneWireBus::formatRom(text, rom_));
        }
        faultBits_ = faults;
    }
    if (faults) {
        failed(sensors::Fault::sensorReported, now);
        return;
    }
    const auto raw = static_cast<std::int16_t>(static_cast<std::uint16_t>(data[1] << 8u | data[0]));
    const std::int32_t milliCelsius = static_cast<std::int32_t>(raw >> 2) * 250; // 14 bit, 0.25 C
    if (milliCelsius < -270000 || milliCelsius > 1372000) {
        failed(sensors::Fault::outOfRange, now);
        return;
    }
    milliCelsius_ = milliCelsius;
    ++samples_;
    if (thermocouple_) thermocouple_->update(milliCelsius, now);
}

void Ct1780Driver::converting(std::uint32_t now) {
    const auto elapsed = static_cast<std::uint32_t>(now - convertAt_);
    bool done = polling_ == Polling::unsupported ? elapsed >= conversionMs : bus_.readBit();
    if (done && polling_ == Polling::unknown) {
        polling_ = elapsed < implausiblyFastMs ? Polling::unsupported : Polling::supported;
        char text[20];
        log_.writef(LogLevel::info, "ct1780 %s: conversion %s", OneWireBus::formatRom(text, rom_),
                    polling_ == Polling::supported ? "polled" : "not reported, fixed wait");
        if (polling_ == Polling::unsupported) done = false;
    }
    if (!done) {
        if (elapsed >= conversionMs && polling_ != Polling::unsupported) {
            failed(sensors::Fault::notReady, now);
            nextAt_ = cycleAt_;
            return;
        }
        nextAt_ = polling_ == Polling::unsupported ? convertAt_ + conversionMs : now + pollMs;
        return;
    }
    lastConversionMs_ = elapsed;
    if (elapsed > maxConversionMs_) maxConversionMs_ = elapsed;
    finish(now);
    nextAt_ = cycleAt_;
}

void Ct1780Driver::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    if (state_ == State::converting) {
        converting(now);
        return;
    }
    cycleAt_ += periodMs_;
    if (static_cast<std::int32_t>(now - cycleAt_) >= 0) cycleAt_ = now + periodMs_;
    nextAt_ = cycleAt_;
    if (rom_ == 0u && !discover(now)) return;
    if (!bus_.select(rom_)) {
        lost();
        failed(sensors::Fault::noResponse, now);
        return;
    }
    bus_.writeByte(commandConvert);
    state_ = State::converting;
    convertAt_ = now;
    nextAt_ = polling_ == Polling::unsupported ? now + conversionMs : now + pollMs;
}

} // namespace slstm32::drivers
