#include "slstm32/drivers/scd4x.hpp"

namespace slstm32::drivers {

namespace {

constexpr std::uint16_t commandStopPeriodic = 0x3F86u;
constexpr std::uint16_t commandStartPeriodic = 0x21B1u;
constexpr std::uint16_t commandReadMeasurement = 0xEC05u;
constexpr std::uint16_t commandDataReady = 0xE4B8u;
constexpr std::uint16_t commandSerialNumber = 0x3682u;
constexpr std::uint16_t commandSensorVariant = 0x202Fu;
constexpr std::uint16_t commandAmbientPressure = 0xE000u;

constexpr std::uint32_t stopMs = 500u;
constexpr std::uint32_t pollMs = 1000u;   // results come every 5 s
constexpr std::uint32_t retryMs = 2000u;
constexpr std::uint32_t commandMs = 1u;

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

const char* variantName(std::uint16_t variant) {
    switch (variant >> 12u) {
    case 0x0u: return "SCD40";
    case 0x1u: return "SCD41";
    case 0x5u: return "SCD43";
    }
    return "SCD4x (unknown variant)";
}

} // namespace

bool Scd4xDriver::command(std::uint16_t code) {
    const std::uint8_t frame[2]{static_cast<std::uint8_t>(code >> 8u), static_cast<std::uint8_t>(code)};
    return bus_.write(address_, frame, sizeof frame);
}

bool Scd4xDriver::command(std::uint16_t code, std::uint16_t argument) {
    std::uint8_t frame[5]{static_cast<std::uint8_t>(code >> 8u), static_cast<std::uint8_t>(code),
                          static_cast<std::uint8_t>(argument >> 8u), static_cast<std::uint8_t>(argument), 0u};
    frame[4] = crc8(frame + 2, 2u);
    return bus_.write(address_, frame, sizeof frame);
}

void Scd4xDriver::wait(std::uint32_t ms) const {
    const auto start = runtime_.millis();
    while (runtime_.millis() - start <= ms) {
    }
}

// Command, wait, then read the words with their CRCs (a separate read: the
// sensor needs time to prepare the answer).
bool Scd4xDriver::readWords(std::uint16_t code, std::uint16_t* words, std::size_t count) {
    if (!command(code)) return false;
    wait(commandMs);
    std::uint8_t data[9]{};
    if (count > 3u || !bus_.read(address_, data, count * 3u)) return false;
    for (std::size_t index = 0u; index < count; ++index) {
        if (crc8(data + 3u * index, 2u) != data[3u * index + 2u]) return false;
        words[index] = static_cast<std::uint16_t>(data[3u * index] << 8u | data[3u * index + 1u]);
    }
    return true;
}

bool Scd4xDriver::init() {
    if (!runtime_.millis) return false;
    if (co2_) co2_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 0u));
    if (temperature_) temperature_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 1u));
    if (humidity_) humidity_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 2u));
    nextAt_ = runtime_.millis();
    return true;
}

void Scd4xDriver::setAmbientPressure(std::uint16_t hectopascal) {
    if (hectopascal >= 700u && hectopascal <= 1200u) pressure_ = hectopascal;
}

void Scd4xDriver::failed(sensors::Fault fault, std::uint32_t now) {
    ++errors_;
    valid_ = false;
    if (fault == sensors::Fault::noResponse) {
        if (present_) log_.writef(LogLevel::warning, "scd4x 0x%02X: no answer", static_cast<unsigned>(address_));
        present_ = false;
        state_ = State::stop;
        nextAt_ = now + retryMs;
    }
    if (co2_) co2_->fail(fault, now);
    if (temperature_) temperature_->fail(fault, now);
    if (humidity_) humidity_->fail(fault, now);
}

void Scd4xDriver::read(std::uint32_t now) {
    std::uint16_t ready = 0u;
    if (!readWords(commandDataReady, &ready, 1u)) {
        failed(sensors::Fault::noResponse, now);
        return;
    }
    if ((ready & 0x07FFu) == 0u) return; // no new result yet
    std::uint16_t words[3]{};
    if (!readWords(commandReadMeasurement, words, 3u)) {
        failed(sensors::Fault::checksum, now);
        return;
    }
    ppm_ = words[0];
    // T = -45 + 175 * raw / 65535, RH = 100 * raw / 65535
    const auto milliCelsius = static_cast<std::int32_t>(static_cast<std::uint64_t>(words[1]) * 175000u / 65535u) - 45000;
    const auto milliPercent = static_cast<std::int32_t>(static_cast<std::uint64_t>(words[2]) * 100000u / 65535u);
    if (ppm_ == 0u || ppm_ > 40000u) {
        failed(sensors::Fault::outOfRange, now);
        return;
    }
    valid_ = true;
    ++samples_;
    if (co2_) co2_->update(static_cast<std::int32_t>(ppm_) * 1000, now);
    if (temperature_) temperature_->update(milliCelsius, now);
    if (humidity_) humidity_->update(milliPercent, now);
}

void Scd4xDriver::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    switch (state_) {
    case State::stop:
        // A measurement may still run from before a reset of the MCU; the
        // sensor accepts other commands only after stopping it.
        if (!command(commandStopPeriodic)) {
            failed(sensors::Fault::noResponse, now);
            return;
        }
        state_ = State::identify;
        nextAt_ = now + stopMs;
        return;
    case State::identify: {
        std::uint16_t serial[3]{};
        if (!readWords(commandSerialNumber, serial, 3u)) {
            failed(sensors::Fault::noResponse, now);
            return;
        }
        serial_ = static_cast<std::uint64_t>(serial[0]) << 32u | static_cast<std::uint64_t>(serial[1]) << 16u | serial[2];
        std::uint16_t variant = 0xFFFFu;
        const bool knowsVariant = readWords(commandSensorVariant, &variant, 1u); // older firmware: no answer
        if (!command(commandStartPeriodic)) {
            failed(sensors::Fault::noResponse, now);
            return;
        }
        log_.writef(LogLevel::info, "scd4x 0x%02X: %s, serial %04X%04X%04X, periodic measurement every 5 s",
                    static_cast<unsigned>(address_), knowsVariant ? variantName(variant) : "SCD4x (variant not reported)",
                    static_cast<unsigned>(serial[0]), static_cast<unsigned>(serial[1]), static_cast<unsigned>(serial[2]));
        present_ = true;
        state_ = State::measuring;
        nextAt_ = now + pollMs;
        return;
    }
    case State::measuring:
        nextAt_ = now + pollMs;
        if (pressure_ != 0u) {
            // Accepted during periodic measurement.
            if (!command(commandAmbientPressure, pressure_)) {
                failed(sensors::Fault::noResponse, now);
                return;
            }
            pressure_ = 0u;
            wait(commandMs);
        }
        read(now);
        return;
    }
}

} // namespace slstm32::drivers
