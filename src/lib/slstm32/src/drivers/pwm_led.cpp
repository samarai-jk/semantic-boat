#include "slstm32/drivers/pwm_led.hpp"

namespace slstm32::drivers {

namespace {
bool reached(std::uint32_t now, std::uint32_t deadline) {
    return static_cast<std::int32_t>(now - deadline) >= 0;
}
}

float PwmLedDriver::clamp(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

bool PwmLedDriver::init() {
    if (!hardware_.write) return false;
    if (hardware_.configure && !hardware_.configure(hardware_.context)) return false;
    if (hardware_.start && !hardware_.start(hardware_.context)) return false;
    write(value_);
    return true;
}

void PwmLedDriver::write(bool value) {
    value_ = value;
    hardware_.write(hardware_.context, value ? brightness_ : 0.0f);
}

void PwmLedDriver::set(bool value) {
    mode_ = Mode::steady;
    write(value);
}

void PwmLedDriver::setBrightness(float brightness) {
    brightness_ = clamp(brightness);
    if (value_) write(true);
}

void PwmLedDriver::pulse(std::uint32_t durationMs) {
    if (!runtime_.millis || durationMs == 0u) return;
    mode_ = Mode::pulse;
    deadline_ = runtime_.millis() + durationMs;
    write(true);
}

void PwmLedDriver::blink(std::uint32_t periodMs, std::uint32_t onTimeMs) {
    if (!runtime_.millis || periodMs == 0u) {
        set(false);
        return;
    }
    mode_ = Mode::blink;
    periodMs_ = periodMs;
    onTimeMs_ = onTimeMs < periodMs ? onTimeMs : periodMs;
    deadline_ = runtime_.millis();
}

void PwmLedDriver::run() {
    if (!runtime_.millis) return;
    const auto now = runtime_.millis();
    if (mode_ == Mode::pulse && reached(now, deadline_)) {
        mode_ = Mode::steady;
        write(false);
    } else if (mode_ == Mode::blink) {
        const auto phase = static_cast<std::uint32_t>(now - deadline_) % periodMs_;
        const bool onNow = phase < onTimeMs_;
        if (onNow != value_) write(onNow);
    }
}

} // namespace slstm32::drivers
