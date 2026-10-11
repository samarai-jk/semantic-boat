#include "slstm32/drivers/led.hpp"

namespace slstm32::drivers {

namespace {
bool reached(std::uint32_t now, std::uint32_t deadline) {
    return static_cast<std::int32_t>(now - deadline) >= 0;
}
}

bool LedDriver::init() {
    if (!hardware_.write) return false;
    if (hardware_.configure && !hardware_.configure(hardware_.context)) return false;
    write(value_);
    return true;
}

void LedDriver::write(bool value) {
    value_ = value;
    hardware_.write(hardware_.context, hardware_.activeHigh ? value : !value);
}

void LedDriver::set(bool value) {
    mode_ = Mode::steady;
    write(value);
}

void LedDriver::pulse(std::uint32_t durationMs) {
    if (!runtime_.millis || durationMs == 0u) return;
    mode_ = Mode::pulse;
    deadline_ = runtime_.millis() + durationMs;
    write(true);
}

void LedDriver::blink(std::uint32_t periodMs, std::uint32_t onTimeMs) {
    if (!runtime_.millis || periodMs == 0u) {
        set(false);
        return;
    }
    mode_ = Mode::blink;
    periodMs_ = periodMs;
    onTimeMs_ = onTimeMs < periodMs ? onTimeMs : periodMs;
    deadline_ = runtime_.millis();
}

void LedDriver::run() {
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
