#include "slstm32/drivers/analog_input.hpp"

namespace slstm32::drivers {

void AnalogInputDriver::resetAverage() {
    sampleIndex_ = 0u;
    sampleCount_ = 0u;
    sampleSum_ = 0;
    raw_ = 0;
    value_ = 0.0f;
    valid_ = false;
}

void AnalogInputDriver::setEnabled(bool enabled) {
    if (enabled != this->enabled()) resetAverage();
    Driver::setEnabled(enabled);
}

bool AnalogInputDriver::setAveragingWindow(std::uint16_t samples) {
    if (samples == 0u || samples > maximumAverageWindow) return false;
    config_.averageWindow = samples;
    resetAverage();
    return true;
}

bool AnalogInputDriver::init() {
    if (!config_.hardware.sample || config_.averageWindow == 0u ||
        config_.averageWindow > maximumAverageWindow) return false;
    if (config_.hardware.configure && !config_.hardware.configure(config_.hardware.context)) return false;
    resetAverage();
    nextSampleAt_ = runtime_.millis ? runtime_.millis() : 0u;
    return sample();
}

bool AnalogInputDriver::sample() {
    std::int32_t reading{};
    if (!config_.hardware.sample(config_.hardware.context, reading)) return false;
    raw_ = reading;
    if (sampleCount_ == config_.averageWindow) {
        sampleSum_ -= samples_[sampleIndex_];
    } else {
        ++sampleCount_;
    }
    samples_[sampleIndex_] = reading;
    sampleSum_ += reading;
    sampleIndex_ = (sampleIndex_ + 1u) % config_.averageWindow;
    const auto average = static_cast<float>(sampleSum_) / static_cast<float>(sampleCount_);
    value_ = (average - config_.offset) * config_.scale;
    valid_ = true;
    return true;
}

void AnalogInputDriver::run() {
    if (config_.samplePeriodMs == 0u || !runtime_.millis) {
        (void)sample();
        return;
    }
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextSampleAt_) < 0) return;
    nextSampleAt_ = now + config_.samplePeriodMs;
    (void)sample();
}

} // namespace slstm32::drivers
