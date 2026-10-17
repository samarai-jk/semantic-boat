#include "slstm32/drivers/tone_pattern.hpp"

namespace slstm32::drivers {

void TonePatternPlayer::play(const TonePattern& pattern) {
    if (!pattern.steps || pattern.count == 0u) {
        stop();
        return;
    }
    pattern_ = &pattern;
    step_ = 0u;
    startStep();
}

void TonePatternPlayer::stop() {
    if (pattern_) tone_.stop();
    pattern_ = nullptr;
}

void TonePatternPlayer::startStep() {
    const auto& step = pattern_->steps[step_];
    stepEndsAt_ = runtime_.millis() + step.durationMs;
    if (step.frequencyHz != 0u && step.durationMs != 0u) {
        (void)tone_.play(step.frequencyHz, step.durationMs, step.level);
    } else {
        tone_.stop();
    }
}

void TonePatternPlayer::run() {
    if (!pattern_ || static_cast<std::int32_t>(runtime_.millis() - stepEndsAt_) < 0) return;
    if (++step_ >= pattern_->count) {
        if (!pattern_->repeat) {
            pattern_ = nullptr;
            return;
        }
        step_ = 0u;
    }
    startStep();
}

} // namespace slstm32::drivers
