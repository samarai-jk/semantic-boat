#include "slstm32/drivers/rgb_led.hpp"

namespace slstm32::drivers {

float RgbLedDriver::clamp(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

bool RgbLedDriver::init() {
    for (auto& channel : channels_) {
        if (!channel.write) return false;
        if (channel.configure && !channel.configure(channel.context)) return false;
        if (channel.start && !channel.start(channel.context)) return false;
    }
    write(color_);
    return true;
}

void RgbLedDriver::write(RgbColor color) {
    const float values[]{clamp(color.red), clamp(color.green), clamp(color.blue)};
    for (std::size_t index = 0; index < channels_.size(); ++index) {
        const float value = commonAnode_ ? 1.0f - values[index] : values[index];
        channels_[index].write(channels_[index].context, value);
    }
}

void RgbLedDriver::setColor(RgbColor color) {
    color_ = {clamp(color.red), clamp(color.green), clamp(color.blue)};
    blinking_ = false;
    outputOn_ = color_.red != 0.0f || color_.green != 0.0f || color_.blue != 0.0f;
    write(color_);
}

void RgbLedDriver::blink(RgbColor color, std::uint32_t periodMs, std::uint32_t onTimeMs) {
    if (!runtime_.millis || periodMs == 0u) {
        setColor(color);
        return;
    }
    color_ = {clamp(color.red), clamp(color.green), clamp(color.blue)};
    blinkColor_ = color_;
    periodMs_ = periodMs;
    onTimeMs_ = onTimeMs < periodMs ? onTimeMs : periodMs;
    startedAt_ = runtime_.millis();
    blinking_ = true;
    outputOn_ = true;
    write(blinkColor_);
}

void RgbLedDriver::run() {
    if (!blinking_ || !runtime_.millis) return;
    const auto phase = static_cast<std::uint32_t>(runtime_.millis() - startedAt_) % periodMs_;
    const bool onNow = phase < onTimeMs_;
    if (onNow == outputOn_) return;
    outputOn_ = onNow;
    write(onNow ? blinkColor_ : RgbColor{});
}

} // namespace slstm32::drivers
