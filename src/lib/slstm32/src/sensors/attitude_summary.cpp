#include "slstm32/sensors/attitude_summary.hpp"
#include <cmath>

namespace slstm32::sensors {

namespace {

constexpr float radiansPerMilliDegree = 3.14159265f / 180000.0f;

std::int32_t deviation(std::int64_t sum, std::int64_t squares, std::uint32_t count) {
    const auto n = static_cast<std::int64_t>(count);
    const auto variance = (squares - sum * sum / n) / n;
    return variance > 0 ? static_cast<std::int32_t>(std::sqrt(static_cast<float>(variance)) + 0.5f) : 0;
}

} // namespace

bool AttitudeSummarizer::init() {
    nextAt_ = runtime_.millis() + intervalMs_;
    reset();
    return true;
}

void AttitudeSummarizer::reset() {
    count_ = 0u;
    headingCount_ = 0u;
    headingSin_ = headingCos_ = 0.0f;
    pitchSum_ = pitchSquares_ = rollSum_ = rollSquares_ = yawRateSum_ = 0;
    yawRateMaximum_ = 0;
}

void AttitudeSummarizer::add(const AttitudeSample& sample) {
    if (count_ == 0u) {
        pitchMinimum_ = pitchMaximum_ = sample.pitch;
        rollMinimum_ = rollMaximum_ = sample.roll;
    }
    ++count_;
    if (sample.headingValid) {
        // Circular mean: 359 and 1 degree average to 0, not 180.
        const float angle = static_cast<float>(sample.heading) * radiansPerMilliDegree;
        headingSin_ += std::sin(angle);
        headingCos_ += std::cos(angle);
        ++headingCount_;
    }
    pitchSum_ += sample.pitch;
    pitchSquares_ += static_cast<std::int64_t>(sample.pitch) * sample.pitch;
    rollSum_ += sample.roll;
    rollSquares_ += static_cast<std::int64_t>(sample.roll) * sample.roll;
    yawRateSum_ += sample.yawRate;
    if (sample.pitch < pitchMinimum_) pitchMinimum_ = sample.pitch;
    if (sample.pitch > pitchMaximum_) pitchMaximum_ = sample.pitch;
    if (sample.roll < rollMinimum_) rollMinimum_ = sample.roll;
    if (sample.roll > rollMaximum_) rollMaximum_ = sample.roll;
    const auto magnitude = sample.yawRate < 0 ? -sample.yawRate : sample.yawRate;
    const auto largest = yawRateMaximum_ < 0 ? -yawRateMaximum_ : yawRateMaximum_;
    if (magnitude > largest) yawRateMaximum_ = sample.yawRate;
}

void AttitudeSummarizer::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    nextAt_ += intervalMs_;
    if (static_cast<std::int32_t>(now - nextAt_) >= 0) nextAt_ = now + intervalMs_;
    if (count_ == 0u) return; // no samples: no summary (the source is missing)
    AttitudeSummary summary{};
    const auto n = static_cast<std::int64_t>(count_);
    summary.samples = count_;
    summary.headingValid = headingCount_ * 2u > count_; // valid for most of the interval
    if (headingCount_ != 0u) {
        auto degrees = std::atan2(headingSin_, headingCos_) / radiansPerMilliDegree;
        if (degrees < 0.0f) degrees += 360000.0f;
        summary.heading = static_cast<std::int32_t>(degrees + 0.5f) % 360000;
    }
    summary.pitchMean = static_cast<std::int32_t>(pitchSum_ / n);
    summary.pitchMinimum = pitchMinimum_;
    summary.pitchMaximum = pitchMaximum_;
    summary.pitchDeviation = deviation(pitchSum_, pitchSquares_, count_);
    summary.rollMean = static_cast<std::int32_t>(rollSum_ / n);
    summary.rollMinimum = rollMinimum_;
    summary.rollMaximum = rollMaximum_;
    summary.rollDeviation = deviation(rollSum_, rollSquares_, count_);
    summary.yawRateMean = static_cast<std::int32_t>(yawRateSum_ / n);
    summary.yawRateMaximum = yawRateMaximum_;
    latest_ = summary;
    reset();
    if (listener_) listener_(listenerContext_, summary);
}

} // namespace slstm32::sensors
