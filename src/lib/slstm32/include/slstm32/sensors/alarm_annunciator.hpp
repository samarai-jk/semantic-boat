#pragma once

#include <cstdint>
#include "slstm32/drivers/tone_pattern.hpp"
#include "slstm32/sensors/registry.hpp"
#include "slstm32/service.hpp"

namespace slstm32::sensors {

// Makes measurement alarms audible and visible. While any alarm waits for
// acknowledgement, the pattern of the most severe unacknowledged one plays
// (so a fridge warning and an engine emergency sound different); after
// acknowledgement the sound stops, and a new alarm sounds again. The
// indicator callback receives every change of the alarm summary, for LEDs or
// a display.
class AlarmAnnunciator final : public Service {
public:
    using Indicator = void (*)(void* context, const AlarmSummary& summary);
    struct Config {
        // Indexed by Severity; a null entry stays silent.
        const drivers::TonePattern* patterns[3]{};
        Indicator indicator{};
        void* indicatorContext{};
    };

    AlarmAnnunciator(MeasurementRegistry& registry, drivers::TonePatternPlayer& player, const Config& config)
        : registry_(registry), player_(player), config_(config) {}

    bool init() override;
    void run() override;
    // Temporarily silences the sound without acknowledging (for example
    // while a service technician works); the next new alarm sounds again.
    void silence();

private:
    static bool same(const AlarmSummary& a, const AlarmSummary& b);

    MeasurementRegistry& registry_;
    drivers::TonePatternPlayer& player_;
    const Config& config_;
    AlarmSummary last_{};
    const drivers::TonePattern* sounding_{};
    bool silenced_{};
};

} // namespace slstm32::sensors
