#pragma once

#include <cstdint>
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace slstm32::sensors {

// Statistics of one interval (usually 1 s) of orientation samples. Angles in
// thousandths of a degree, rates in thousandths of a degree per second.
// Pitch: bow up positive; roll: starboard down positive; yaw rate: turning to
// starboard positive (the source has to deliver these signs).
struct AttitudeSummary {
    std::uint32_t samples{};
    bool headingValid{};       // the source considered the heading usable
    std::int32_t heading{};    // circular mean, 0..360000
    std::int32_t pitchMean{};
    std::int32_t pitchMinimum{};
    std::int32_t pitchMaximum{};
    std::int32_t pitchDeviation{}; // standard deviation within the interval
    std::int32_t rollMean{};
    std::int32_t rollMinimum{};
    std::int32_t rollMaximum{};
    std::int32_t rollDeviation{};
    std::int32_t yawRateMean{};
    std::int32_t yawRateMaximum{}; // largest magnitude, with its sign
};

struct AttitudeSample {
    std::int32_t heading{}; // m°, 0..360000
    std::int32_t pitch{};
    std::int32_t roll{};
    std::int32_t yawRate{}; // m°/s
    bool headingValid{};
};

// Reduces a fast orientation stream (10..100 Hz) to one AttitudeSummary per
// interval, for logging and the bus: what happened within the second (mean,
// extremes, spread), not every sample. Feed it with add() from the sensor
// driver's sample callback; the listener receives each finished summary.
class AttitudeSummarizer final : public Service {
public:
    using Listener = void (*)(void* context, const AttitudeSummary& summary);

    AttitudeSummarizer(Runtime runtime, std::uint32_t intervalMs = 1000u) : runtime_(runtime), intervalMs_(intervalMs) {}

    bool init() override;
    void run() override;
    void add(const AttitudeSample& sample);
    void setListener(Listener listener, void* context) {
        listener_ = listener;
        listenerContext_ = context;
    }
    // The last finished summary (samples == 0 before the first).
    const AttitudeSummary& latest() const { return latest_; }

private:
    void reset();

    Runtime runtime_;
    std::uint32_t intervalMs_{};
    std::uint32_t nextAt_{};
    Listener listener_{};
    void* listenerContext_{};
    AttitudeSummary latest_{};
    // Running sums of the current interval.
    std::uint32_t count_{};
    std::uint32_t headingCount_{};
    float headingSin_{};
    float headingCos_{};
    std::int64_t pitchSum_{};
    std::int64_t pitchSquares_{};
    std::int64_t rollSum_{};
    std::int64_t rollSquares_{};
    std::int64_t yawRateSum_{};
    std::int32_t pitchMinimum_{};
    std::int32_t pitchMaximum_{};
    std::int32_t rollMinimum_{};
    std::int32_t rollMaximum_{};
    std::int32_t yawRateMaximum_{};
};

} // namespace slstm32::sensors
