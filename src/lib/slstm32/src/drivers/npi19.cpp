#include "slstm32/drivers/npi19.hpp"

namespace slstm32::drivers {

namespace {

constexpr std::uint32_t powerUpMs = 10u;
constexpr std::uint32_t conversionMs = 10u;
constexpr std::uint32_t busyRetryMs = 5u;
constexpr std::uint8_t maxBusyRetries = 10u;
constexpr std::uint8_t commandFullMeasurement = 0xACu;
constexpr std::uint8_t statusFresh = 0u;
constexpr std::uint8_t statusStale = 2u;
constexpr std::int32_t countsMin = 1638;
constexpr std::int32_t countsMax = 14746;
// Readings beyond the range by more than this are implausible.
constexpr std::int32_t countsMargin = 1000;
constexpr std::uint32_t rangeWarningPeriodMs = 60000u;

} // namespace

bool Npi19Driver::init() {
    if (!runtime_.millis) return false;
    if (pressure_) pressure_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 0u));
    if (temperature_) temperature_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 1u));
    cycleAt_ = nextAt_ = runtime_.millis() + powerUpMs;
    return true;
}

void Npi19Driver::failed(sensors::Fault fault, std::uint32_t now) {
    ++errors_;
    valid_ = false;
    state_ = State::idle;
    if (present_ && fault == sensors::Fault::noResponse) {
        log_.writef(LogLevel::warning, "npi19 0x%02X: no answer", static_cast<unsigned>(address_));
    }
    if (fault == sensors::Fault::noResponse) present_ = false;
    if (pressure_) pressure_->fail(fault, now);
    if (temperature_) temperature_->fail(fault, now);
}

void Npi19Driver::finish(std::uint32_t now) {
    std::uint8_t data[4]{};
    if (!bus_.read(address_, data, sizeof data)) {
        failed(sensors::Fault::noResponse, now);
        return;
    }
    status_ = static_cast<std::uint8_t>(data[0] >> 6u);
    if (status_ == statusStale) {
        if (++busyRetries_ > maxBusyRetries) {
            failed(sensors::Fault::notReady, now);
            return;
        }
        nextAt_ = now + busyRetryMs;
        return;
    }
    state_ = State::idle;
    bridgeCounts_ = static_cast<std::uint16_t>((data[0] & 0x3Fu) << 8u | data[1]);
    temperatureCounts_ = static_cast<std::uint16_t>(data[2] << 3u | data[3] >> 5u);
    if (!present_) {
        log_.writef(LogLevel::info, "npi19 0x%02X: measuring, status %u, bridge %u, temperature %u",
                    static_cast<unsigned>(address_), static_cast<unsigned>(status_),
                    static_cast<unsigned>(bridgeCounts_), static_cast<unsigned>(temperatureCounts_));
        present_ = true;
    }
    const std::int32_t counts = bridgeCounts_;
    const bool fresh = status_ == statusFresh;
    if (!fresh || counts < countsMin - countsMargin || counts > countsMax + countsMargin) {
        // Logged on the first bad reading and once a minute while it lasts.
        if (!rangeWarned_ || now - rangeWarnedAt_ >= rangeWarningPeriodMs) {
            log_.writef(LogLevel::warning, "npi19 0x%02X: status %u, bridge %u, temperature %u",
                        static_cast<unsigned>(address_), static_cast<unsigned>(status_),
                        static_cast<unsigned>(bridgeCounts_), static_cast<unsigned>(temperatureCounts_));
            rangeWarned_ = true;
            rangeWarnedAt_ = now;
        }
        failed(fresh ? sensors::Fault::outOfRange : sensors::Fault::sensorReported, now);
        return;
    }
    rangeWarned_ = false;
    pressureValue_ = zeroScale_ + static_cast<std::int32_t>(static_cast<std::int64_t>(counts - countsMin) *
                                                            (fullScale_ - zeroScale_) / (countsMax - countsMin));
    // -50..150 °C over 11 bits.
    milliCelsius_ = static_cast<std::int32_t>(temperatureCounts_) * 200000 / 2047 - 50000;
    valid_ = true;
    ++samples_;
    if (pressure_) {
        pressure_->update(pressureValue_, now);
        pressure_->setRawValue(counts);
    }
    if (temperature_) {
        temperature_->update(milliCelsius_, now);
        temperature_->setRawValue(temperatureCounts_);
    }
}

void Npi19Driver::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    if (state_ == State::converting) {
        finish(now);
        if (state_ == State::converting) return; // still busy, retry scheduled
        nextAt_ = cycleAt_;
        return;
    }
    cycleAt_ += periodMs_;
    if (static_cast<std::int32_t>(now - cycleAt_) >= 0) cycleAt_ = now + periodMs_;
    const std::uint8_t command = commandFullMeasurement;
    if (!bus_.write(address_, &command, 1u)) {
        failed(sensors::Fault::noResponse, now);
        nextAt_ = cycleAt_;
        return;
    }
    state_ = State::converting;
    busyRetries_ = 0u;
    nextAt_ = now + conversionMs;
}

} // namespace slstm32::drivers
