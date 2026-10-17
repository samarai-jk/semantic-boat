#include "slstm32/drivers/sht31.hpp"

namespace slstm32::drivers {

namespace {

constexpr std::uint32_t powerUpMs = 2u;
constexpr std::uint32_t resetMs = 2u;
constexpr std::uint32_t conversionMs = 20u; // 15.5 ms maximum at high repeatability
constexpr std::uint32_t readRetryMs = 5u;
constexpr std::uint8_t maxReadRetries = 4u;
constexpr std::uint16_t measureHighRepeatability = 0x2400u;
constexpr std::uint16_t softReset = 0x30A2u;
constexpr std::uint16_t heaterOn = 0x306Du;
constexpr std::uint16_t heaterOff = 0x3066u;

// Sensirion CRC-8: polynomial 0x31, initial value 0xFF.
std::uint8_t crc8(const std::uint8_t* data, std::size_t size) {
    std::uint8_t crc = 0xFFu;
    for (std::size_t index = 0u; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            crc = static_cast<std::uint8_t>(crc & 0x80u ? (crc << 1u) ^ 0x31u : crc << 1u);
        }
    }
    return crc;
}

} // namespace

bool Sht31Driver::init() {
    if (!runtime_.millis) return false;
    if (temperature_) temperature_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 0u));
    if (humidity_) humidity_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 1u));
    cycleAt_ = nextAt_ = runtime_.millis() + powerUpMs;
    return true;
}

bool Sht31Driver::command(std::uint16_t code) {
    const std::uint8_t frame[2]{static_cast<std::uint8_t>(code >> 8u), static_cast<std::uint8_t>(code)};
    return bus_.write(address_, frame, sizeof frame);
}

bool Sht31Driver::setHeater(bool on) {
    if (state_ == State::converting) return false; // commands are ignored during a measurement
    if (!command(on ? heaterOn : heaterOff)) return false;
    heater_ = on;
    return true;
}

void Sht31Driver::failed(sensors::Fault fault, std::uint32_t now) {
    ++errors_;
    valid_ = false;
    state_ = State::idle;
    if (fault == sensors::Fault::noResponse) {
        if (present_) log_.writef(LogLevel::warning, "sht31 0x%02X: no answer", static_cast<unsigned>(address_));
        present_ = false;
    }
    if (temperature_) temperature_->fail(fault, now);
    if (humidity_) humidity_->fail(fault, now);
}

void Sht31Driver::finish(std::uint32_t now) {
    std::uint8_t data[6]{};
    // The sensor does not acknowledge the read while still measuring.
    if (!bus_.read(address_, data, sizeof data)) {
        if (++readRetries_ > maxReadRetries) {
            failed(sensors::Fault::noResponse, now);
            return;
        }
        nextAt_ = now + readRetryMs;
        return;
    }
    state_ = State::idle;
    if (crc8(data, 2u) != data[2] || crc8(data + 3, 2u) != data[5]) {
        failed(sensors::Fault::checksum, now);
        return;
    }
    const std::uint32_t rawTemperature = static_cast<std::uint32_t>(data[0]) << 8u | data[1];
    const std::uint32_t rawHumidity = static_cast<std::uint32_t>(data[3]) << 8u | data[4];
    // T = -45 + 175 * raw / 65535, RH = 100 * raw / 65535
    milliCelsius_ = static_cast<std::int32_t>(static_cast<std::uint64_t>(rawTemperature) * 175000u / 65535u) - 45000;
    milliPercent_ = static_cast<std::int32_t>(static_cast<std::uint64_t>(rawHumidity) * 100000u / 65535u);
    if (milliCelsius_ < -40000 || milliCelsius_ > 125000) {
        failed(sensors::Fault::outOfRange, now);
        return;
    }
    valid_ = true;
    ++samples_;
    if (temperature_) temperature_->update(milliCelsius_, now);
    if (humidity_) humidity_->update(milliPercent_, now);
}

void Sht31Driver::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    if (state_ == State::converting) {
        finish(now);
        if (state_ == State::idle) nextAt_ = cycleAt_;
        return;
    }
    cycleAt_ += periodMs_;
    if (static_cast<std::int32_t>(now - cycleAt_) >= 0) cycleAt_ = now + periodMs_;
    if (!present_) {
        // First contact (or back after a loss): start from a known state.
        if (!command(softReset)) {
            failed(sensors::Fault::noResponse, now);
            nextAt_ = cycleAt_;
            return;
        }
        present_ = true;
        heater_ = false;
        log_.writef(LogLevel::info, "sht31 0x%02X: measuring", static_cast<unsigned>(address_));
        nextAt_ = now + resetMs;
        cycleAt_ = now; // measure right after the reset
        return;
    }
    if (!command(measureHighRepeatability)) {
        failed(sensors::Fault::noResponse, now);
        nextAt_ = cycleAt_;
        return;
    }
    state_ = State::converting;
    readRetries_ = 0u;
    nextAt_ = now + conversionMs;
}

} // namespace slstm32::drivers
