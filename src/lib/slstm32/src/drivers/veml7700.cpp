#include "slstm32/drivers/veml7700.hpp"

namespace slstm32::drivers {

namespace {

constexpr std::uint8_t registerConfiguration = 0x00u;
constexpr std::uint8_t registerPowerSaving = 0x03u;
constexpr std::uint8_t registerAls = 0x04u;
constexpr std::uint8_t registerId = 0x07u;

// ALS_CONF fields: gain in bits 12:11, integration time in bits 9:6.
constexpr std::uint16_t gain2 = 0x1u << 11u;
constexpr std::uint16_t gain1 = 0x0u << 11u;
constexpr std::uint16_t gainQuarter = 0x3u << 11u;
constexpr std::uint16_t gainEighth = 0x2u << 11u;
constexpr std::uint16_t it25 = 0xCu << 6u;
constexpr std::uint16_t it50 = 0x8u << 6u;
constexpr std::uint16_t it100 = 0x0u << 6u;
constexpr std::uint16_t it200 = 0x1u << 6u;
constexpr std::uint16_t it400 = 0x2u << 6u;
constexpr std::uint16_t it800 = 0x3u << 6u;

struct Range {
    std::uint16_t configuration;
    std::uint16_t integrationMs;
    std::uint32_t microLuxPerCount;
    bool corrected; // gain 1/4 and 1/8
};

// From most to least sensitive; 3600 ulx/count at gain 2 and 800 ms.
constexpr Range ranges[]{
    {gain2 | it800, 800u, 3600u, false},           {gain2 | it400, 400u, 7200u, false},
    {gain2 | it200, 200u, 14400u, false},          {gain2 | it100, 100u, 28800u, false},
    {gain1 | it100, 100u, 57600u, false},          {gainQuarter | it100, 100u, 230400u, true},
    {gainEighth | it100, 100u, 460800u, true},     {gainEighth | it50, 50u, 921600u, true},
    {gainEighth | it25, 25u, 1843200u, true},
};
constexpr std::uint8_t rangeCount = sizeof ranges / sizeof ranges[0];
constexpr std::uint16_t countsHigh = 20000u;
constexpr std::uint16_t countsLow = 200u;

// Vishay application note 84323: correction for the low gains.
std::int32_t correct(std::int32_t milliLux) {
    const float lux = static_cast<float>(milliLux) / 1000.0f;
    const float corrected = (((6.0135e-13f * lux - 9.3924e-9f) * lux + 8.1488e-5f) * lux + 1.0023f) * lux;
    return static_cast<std::int32_t>(corrected * 1000.0f + 0.5f);
}

} // namespace

bool Veml7700Driver::writeRegister(std::uint8_t reg, std::uint16_t value) {
    const std::uint8_t frame[3]{reg, static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8u)};
    return bus_.write(address_, frame, sizeof frame);
}

bool Veml7700Driver::readRegister(std::uint8_t reg, std::uint16_t& value) {
    std::uint8_t data[2]{};
    if (!bus_.readRegister(address_, reg, data, sizeof data)) return false;
    value = static_cast<std::uint16_t>(data[0] | data[1] << 8u);
    return true;
}

bool Veml7700Driver::init() {
    if (!runtime_.millis) return false;
    if (illuminance_) illuminance_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 0u));
    nextAt_ = runtime_.millis();
    return true;
}

// Power on (SD = 0) with the current range, no interrupts, no power saving.
// The first result is ready after two integration times.
bool Veml7700Driver::configure() {
    if (!writeRegister(registerConfiguration, ranges[step_].configuration) || !writeRegister(registerPowerSaving, 0u)) {
        return false;
    }
    nextAt_ = runtime_.millis() + 2u * ranges[step_].integrationMs + 10u;
    return true;
}

void Veml7700Driver::failed(std::uint32_t now) {
    ++errors_;
    valid_ = false;
    if (configured_) log_.writef(LogLevel::warning, "veml7700 0x%02X: no answer", static_cast<unsigned>(address_));
    configured_ = false;
    if (illuminance_) illuminance_->fail(sensors::Fault::noResponse, now);
    nextAt_ = now + periodMs_;
}

void Veml7700Driver::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    if (!configured_) {
        if (!configure()) {
            failed(now);
            return;
        }
        std::uint16_t id = 0u;
        (void)readRegister(registerId, id);
        log_.writef(LogLevel::info, "veml7700 0x%02X: measuring, id 0x%04X", static_cast<unsigned>(address_),
                    static_cast<unsigned>(id));
        configured_ = true;
        return;
    }
    std::uint16_t counts = 0u;
    if (!readRegister(registerAls, counts)) {
        failed(now);
        return;
    }
    counts_ = counts;
    const auto& range = ranges[step_];
    // Adjust the range first; a reading outside the good window is still
    // used unless it saturated.
    std::uint8_t step = step_;
    if (counts > countsHigh && step + 1u < rangeCount) {
        ++step;
    } else if (counts < countsLow && step > 0u) {
        --step;
    }
    if (counts != 0xFFFFu || step == step_) {
        auto milliLux = static_cast<std::int32_t>(static_cast<std::uint64_t>(counts) * range.microLuxPerCount / 1000u);
        if (range.corrected) milliLux = correct(milliLux);
        milliLux_ = milliLux;
        valid_ = true;
        ++samples_;
        if (illuminance_) illuminance_->update(milliLux, now);
    }
    if (step != step_) {
        step_ = step;
        if (!configure()) failed(now);
        return;
    }
    const std::uint32_t interval = range.integrationMs + range.integrationMs / 10u;
    nextAt_ = now + (periodMs_ > interval ? periodMs_ : interval);
}

} // namespace slstm32::drivers
