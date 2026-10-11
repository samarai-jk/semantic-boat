#include "slstm32/drivers/tone.hpp"

namespace slstm32::drivers {

bool ToneDriver::init() {
    if (!hardware_.start || !hardware_.stop || !runtime_.millis) return false;
    if (hardware_.configure && !hardware_.configure(hardware_.context)) return false;
    hardware_.stop(hardware_.context);
    return true;
}

bool ToneDriver::play(std::uint32_t frequencyHz, std::uint32_t durationMs, float level) {
    if (frequencyHz == 0u || durationMs == 0u || level <= 0.0f) return false;
    if (level > 1.0f) level = 1.0f;
    // Publish no active tone while replacing one. This prevents an interrupt
    // servicing the old deadline from stopping the newly configured PWM.
    playing_ = false;
    stopAt_ = runtime_.millis() + durationMs;
    if (!hardware_.start(hardware_.context, frequencyHz, level)) return false;
    playing_ = true;
    return true;
}

void ToneDriver::stop() {
    hardware_.stop(hardware_.context);
    playing_ = false;
}

void ToneDriver::run() {
    if (playing_ && static_cast<std::int32_t>(runtime_.millis() - stopAt_) >= 0) stop();
}

void ToneDriver::onTick() {
    run();
}

} // namespace slstm32::drivers
