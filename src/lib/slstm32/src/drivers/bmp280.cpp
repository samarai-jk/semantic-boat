#include "slstm32/drivers/bmp280.hpp"

namespace slstm32::drivers {

namespace {

constexpr std::uint8_t registerTrimming = 0x88u; // 24 bytes
constexpr std::uint8_t registerChipId = 0xD0u;
constexpr std::uint8_t registerReset = 0xE0u;
constexpr std::uint8_t registerStatus = 0xF3u;   // bit 3: measuring
constexpr std::uint8_t registerControl = 0xF4u;  // osrs_t 7:5, osrs_p 4:2, mode 1:0
constexpr std::uint8_t registerConfig = 0xF5u;
constexpr std::uint8_t registerData = 0xF7u;     // pressure MSB, LSB, XLSB, temperature MSB, LSB, XLSB
constexpr std::uint8_t chipIdBmp280 = 0x58u;
constexpr std::uint8_t chipIdBme280 = 0x60u;
constexpr std::uint8_t commandReset = 0xB6u;
constexpr std::uint8_t controlForced = (2u << 5u) | (5u << 2u) | 1u; // temperature x2, pressure x16, forced

constexpr std::uint32_t firstPollMs = 40u;
constexpr std::uint32_t pollMs = 2u;
constexpr std::uint32_t timeoutMs = 200u;

std::uint16_t u16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | data[1] << 8u);
}

std::int16_t s16(const std::uint8_t* data) {
    return static_cast<std::int16_t>(u16(data));
}

} // namespace

bool Bmp280Driver::init() {
    if (!runtime_.millis) return false;
    if (pressure_) pressure_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 0u));
    if (temperature_) temperature_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 1u));
    cycleAt_ = nextAt_ = runtime_.millis();
    return true;
}

// Blocks about 3 ms (reset), once per power-up or reconnection.
bool Bmp280Driver::configure() {
    std::uint8_t id = 0u;
    if (!bus_.readRegister(address_, registerChipId, &id, 1u)) return false;
    if (id != chipIdBmp280 && id != chipIdBme280) {
        log_.writef(LogLevel::warning, "bmp280 0x%02X: chip ID 0x%02X is not a BMP280/BME280",
                    static_cast<unsigned>(address_), static_cast<unsigned>(id));
        return false;
    }
    if (!bus_.writeRegister(address_, registerReset, commandReset)) return false;
    const auto start = runtime_.millis();
    while (runtime_.millis() - start <= 2u) {
    }
    std::uint8_t data[24]{};
    if (!bus_.readRegister(address_, registerTrimming, data, sizeof data) ||
        !bus_.writeRegister(address_, registerConfig, 0x00u)) { // no IIR filter
        return false;
    }
    trim_ = {u16(data),      s16(data + 2),  s16(data + 4),  u16(data + 6),  s16(data + 8),  s16(data + 10),
             s16(data + 12), s16(data + 14), s16(data + 16), s16(data + 18), s16(data + 20), s16(data + 22)};
    log_.writef(LogLevel::info, "bmp280 0x%02X: %s, pressure x16, temperature x2", static_cast<unsigned>(address_),
                id == chipIdBme280 ? "BME280 (humidity not read)" : "BMP280");
    return true;
}

void Bmp280Driver::failed(sensors::Fault fault, std::uint32_t now) {
    valid_ = false;
    state_ = State::idle;
    if (fault == sensors::Fault::noResponse) {
        if (configured_) log_.writef(LogLevel::warning, "bmp280 0x%02X: no answer", static_cast<unsigned>(address_));
        configured_ = false;
    }
    if (pressure_) pressure_->fail(fault, now);
    if (temperature_) temperature_->fail(fault, now);
}

void Bmp280Driver::finish(std::uint32_t now) {
    std::uint8_t data[6]{};
    if (!bus_.readRegister(address_, registerData, data, sizeof data)) {
        failed(sensors::Fault::noResponse, now);
        return;
    }
    state_ = State::idle;
    const std::int32_t adcP = static_cast<std::int32_t>(data[0]) << 12 | static_cast<std::int32_t>(data[1]) << 4 |
                              data[2] >> 4;
    const std::int32_t adcT = static_cast<std::int32_t>(data[3]) << 12 | static_cast<std::int32_t>(data[4]) << 4 |
                              data[5] >> 4;
    if (adcP == 0x80000 || adcT == 0x80000) { // skipped measurement
        failed(sensors::Fault::notReady, now);
        return;
    }
    // Bosch BMP280 datasheet, section 8.2 (32-bit temperature, 64-bit pressure).
    const std::int32_t t1 = trim_.t1;
    std::int32_t var1 = (((adcT >> 3) - (t1 << 1)) * trim_.t2) >> 11;
    std::int32_t var2 = (((((adcT >> 4) - t1) * ((adcT >> 4) - t1)) >> 12) * trim_.t3) >> 14;
    const std::int32_t fine = var1 + var2;
    milliCelsius_ = ((fine * 5 + 128) >> 8) * 10; // 0.01 C -> m°C
    std::int64_t p1 = static_cast<std::int64_t>(fine) - 128000;
    std::int64_t p2 = p1 * p1 * trim_.p6;
    p2 += (p1 * trim_.p5) << 17;
    p2 += static_cast<std::int64_t>(trim_.p4) << 35;
    p1 = ((p1 * p1 * trim_.p3) >> 8) + ((p1 * trim_.p2) << 12);
    p1 = ((static_cast<std::int64_t>(1) << 47) + p1) * trim_.p1 >> 33;
    if (p1 == 0) {
        failed(sensors::Fault::outOfRange, now);
        return;
    }
    std::int64_t pressure = 1048576 - adcP;
    pressure = (((pressure << 31) - p2) * 3125) / p1;
    p1 = (static_cast<std::int64_t>(trim_.p9) * (pressure >> 13) * (pressure >> 13)) >> 25;
    p2 = (static_cast<std::int64_t>(trim_.p8) * pressure) >> 19;
    pressure = ((pressure + p1 + p2) >> 8) + (static_cast<std::int64_t>(trim_.p7) << 4); // Q24.8 Pa
    pressure_dPa_ = static_cast<std::int32_t>(pressure * 10 / 256);
    if (pressure_dPa_ < 300000 || pressure_dPa_ > 1100000) {
        failed(sensors::Fault::outOfRange, now);
        return;
    }
    valid_ = true;
    if (pressure_) pressure_->update(pressure_dPa_, now);
    if (temperature_) temperature_->update(milliCelsius_, now);
}

void Bmp280Driver::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    if (state_ == State::converting) {
        std::uint8_t status = 0u;
        if (!bus_.readRegister(address_, registerStatus, &status, 1u)) {
            failed(sensors::Fault::noResponse, now);
            nextAt_ = cycleAt_;
            return;
        }
        if (!(status & 0x08u)) {
            finish(now);
            nextAt_ = cycleAt_;
        } else if (static_cast<std::uint32_t>(now - startedAt_) >= timeoutMs) {
            failed(sensors::Fault::notReady, now);
            nextAt_ = cycleAt_;
        } else {
            nextAt_ = now + pollMs;
        }
        return;
    }
    cycleAt_ += periodMs_;
    if (static_cast<std::int32_t>(now - cycleAt_) >= 0) cycleAt_ = now + periodMs_;
    nextAt_ = cycleAt_;
    if (!configured_) {
        if (!configure()) {
            failed(sensors::Fault::noResponse, now);
            return;
        }
        configured_ = true;
    }
    if (!bus_.writeRegister(address_, registerControl, controlForced)) {
        failed(sensors::Fault::noResponse, now);
        return;
    }
    state_ = State::converting;
    startedAt_ = now;
    nextAt_ = now + firstPollMs;
}

} // namespace slstm32::drivers
