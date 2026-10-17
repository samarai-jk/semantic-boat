#include "slstm32/drivers/ds18b20.hpp"
#include <cstdio>

namespace slstm32::drivers {

namespace {

constexpr std::uint8_t familyDs18b20 = 0x28u;
constexpr std::uint8_t familyDs1822 = 0x22u;
constexpr std::uint8_t commandConvert = 0x44u;
constexpr std::uint8_t commandReadScratchpad = 0xBEu;
constexpr std::int16_t powerOnValue = 0x0550; // 85 C

} // namespace

const char* Ds18b20Driver::formatRom(char (&buffer)[20], OneWireBus::Rom rom) {
    const auto serial = rom >> 8u;
    std::snprintf(buffer, sizeof buffer, "%02x-%04lx%08lx", static_cast<unsigned>(rom & 0xFFu),
                  static_cast<unsigned long>((serial >> 32u) & 0xFFFFu),
                  static_cast<unsigned long>(serial & 0xFFFFFFFFu));
    return buffer;
}

bool Ds18b20Driver::init() {
    if (!runtime_.millis || !bus_.init()) return false;
    cycleAt_ = nextAt_ = runtime_.millis();
    return true;
}

void Ds18b20Driver::failed(sensors::Fault fault, std::uint32_t now) {
    ++errors_;
    valid_ = false;
    state_ = State::idle;
    if (temperature_) temperature_->fail(fault, now);
}

bool Ds18b20Driver::discover(std::uint32_t now) {
    OneWireBus::Rom rom = 0u;
    if (!bus_.readRom(rom)) {
        if (!bus_.idleHigh() && !lineWarned_) {
            log_.write(LogLevel::warning, "ds18b20: 1-Wire line stays low (short, or no pull-up?)");
            lineWarned_ = true;
        }
        if (rom_ != 0u) {
            char text[20];
            log_.writef(LogLevel::warning, "ds18b20 %s: lost", formatRom(text, rom_));
            rom_ = 0u;
        }
        failed(sensors::Fault::noResponse, now);
        return false;
    }
    lineWarned_ = false;
    char text[20];
    const auto family = OneWireBus::family(rom);
    if (family != familyDs18b20 && family != familyDs1822) {
        log_.writef(LogLevel::warning, "ds18b20: %s on the bus is not a DS18B20", formatRom(text, rom));
        failed(sensors::Fault::sensorReported, now);
        return false;
    }
    rom_ = rom;
    firstConversion_ = true;
    if (temperature_) temperature_->setIdentity(sensors::Identity::oneWire(rom));
    std::uint8_t scratchpad[9];
    if (readScratchpad(scratchpad)) {
        resolutionBits_ = static_cast<std::uint8_t>(9u + ((scratchpad[4] >> 5u) & 0x03u));
        conversionMs_ = 750u >> (12u - resolutionBits_);
    }
    log_.writef(LogLevel::info, "ds18b20 %s: found, %u bit", formatRom(text, rom),
                static_cast<unsigned>(resolutionBits_));
    return true;
}

bool Ds18b20Driver::readScratchpad(std::uint8_t (&data)[9]) {
    if (!bus_.select(rom_)) return false;
    bus_.writeByte(commandReadScratchpad);
    bus_.read(data, sizeof data);
    // Bits 0..4 of the configuration register always read 1; this rejects
    // an all-zero frame, which passes the CRC.
    return OneWireBus::crc8(data, 8u) == data[8] && (data[4] & 0x1Fu) == 0x1Fu;
}

void Ds18b20Driver::finish(std::uint32_t now) {
    state_ = State::idle;
    std::uint8_t data[9];
    if (!readScratchpad(data)) {
        // Look for the sensor again next period: a swapped sensor answers
        // the reset but ignores the old ROM code and reads as all ones.
        const bool present = bus_.reset();
        if (!present) {
            char text[20];
            log_.writef(LogLevel::warning, "ds18b20 %s: lost", formatRom(text, rom_));
        }
        rom_ = 0u;
        failed(present ? sensors::Fault::checksum : sensors::Fault::noResponse, now);
        return;
    }
    auto raw = static_cast<std::int16_t>(static_cast<std::uint16_t>(data[1] << 8u | data[0]));
    // Below 12 bit the lowest bits are undefined.
    raw = static_cast<std::int16_t>(raw & ~((1 << (12u - resolutionBits_)) - 1));
    if (firstConversion_ && raw == powerOnValue) {
        firstConversion_ = false;
        return; // the first conversion after power-up did not run yet
    }
    firstConversion_ = false;
    milliCelsius_ = static_cast<std::int32_t>(raw) * 125 / 2; // 1/16 C per bit
    if (milliCelsius_ < -55000 || milliCelsius_ > 125000) {
        failed(sensors::Fault::outOfRange, now);
        return;
    }
    valid_ = true;
    ++samples_;
    if (temperature_) temperature_->update(milliCelsius_, now);
}

void Ds18b20Driver::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    if (state_ == State::converting) {
        finish(now);
        nextAt_ = cycleAt_;
        return;
    }
    cycleAt_ += periodMs_;
    if (static_cast<std::int32_t>(now - cycleAt_) >= 0) cycleAt_ = now + periodMs_;
    nextAt_ = cycleAt_;
    if (rom_ == 0u && !discover(now)) return;
    if (!bus_.select(rom_)) {
        char text[20];
        log_.writef(LogLevel::warning, "ds18b20 %s: lost", formatRom(text, rom_));
        rom_ = 0u;
        failed(sensors::Fault::noResponse, now);
        return;
    }
    bus_.writeByte(commandConvert);
    state_ = State::converting;
    // 10 % margin over the datasheet maximum.
    nextAt_ = now + conversionMs_ + conversionMs_ / 10u;
}

} // namespace slstm32::drivers
