#include "slstm32/drivers/frequency_input.hpp"

namespace slstm32::drivers {

bool FrequencyInput::init() {
    if (!runtime_.millis || !config_.ticks || config_.ticksPerSecond == 0u || config_.milliPulsesPerUnit == 0u) {
        return false;
    }
    minimumTicks_ = static_cast<std::uint32_t>(static_cast<std::uint64_t>(config_.minimumPeriodUs) *
                                               config_.ticksPerSecond / 1000000u);
    nextAt_ = runtime_.millis() + config_.periodMs;
    return true;
}

void FrequencyInput::onEdge() {
    const auto now = config_.ticks();
    if (totalEdges_ != 0u && minimumTicks_ != 0u && now - lastTick_ < minimumTicks_) {
        glitches_ = glitches_ + 1u;
        return;
    }
    if (count_ == 0u) firstTick_ = now;
    previousTick_ = lastTick_;
    lastTick_ = now;
    count_ = count_ + 1u;
    totalEdges_ = totalEdges_ + 1u;
    lastEdgeMs_ = runtime_.millis();
}

void FrequencyInput::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    nextAt_ += config_.periodMs;
    if (static_cast<std::int32_t>(now - nextAt_) >= 0) nextAt_ = now + config_.periodMs;

    std::uint32_t count;
    std::uint32_t first;
    std::uint32_t last;
    std::uint32_t previous;
    std::uint32_t lastEdgeMs;
    std::uint32_t total;
    {
        CriticalSection critical{runtime_};
        count = count_;
        first = firstTick_;
        last = lastTick_;
        previous = previousTick_;
        lastEdgeMs = lastEdgeMs_;
        total = totalEdges_;
        // The next period continues from the last edge of this one.
        count_ = count != 0u ? 1u : 0u;
        firstTick_ = last;
    }

    std::uint64_t ticks = 0u;
    std::uint64_t periods = 0u;
    if (count >= 2u) {
        ticks = last - first;
        periods = count - 1u;
    } else if (total >= 2u && static_cast<std::uint32_t>(now - lastEdgeMs) < config_.timeoutMs) {
        ticks = last - previous; // slower than one edge per period: the last full period
        periods = 1u;
    }
    if (static_cast<std::uint32_t>(now - lastEdgeMs) >= config_.timeoutMs || total < 2u) ticks = 0u;
    milliHertz_ = ticks ? static_cast<std::uint32_t>(periods * config_.ticksPerSecond * 1000u / ticks) : 0u;
    // value = f / pulses per unit (x 60 per minute), in thousandths of a unit.
    const std::uint64_t scale = config_.perMinute ? 60u : 1u;
    value_ = static_cast<std::int32_t>(static_cast<std::uint64_t>(milliHertz_) * scale * 1000u / config_.milliPulsesPerUnit);
    if (channel_) channel_->update(value_, now);
}

} // namespace slstm32::drivers
