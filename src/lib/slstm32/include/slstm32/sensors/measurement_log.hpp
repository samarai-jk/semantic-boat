#pragma once

#include <cstdint>
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/registry.hpp"
#include "slstm32/service.hpp"

namespace slstm32::sensors {

// Writes the values of all channels of a registry to the log periodically,
// as lines like "values: board 27.81 C | air 22.40 C | air-rh 45.31 %".
// Channels with a slow trend add its change and span: "(-0.42/95m)".
// Channels without a valid value show their status instead. Long lists are
// split over several lines.
class MeasurementLog final : public Service {
public:
    MeasurementLog(Runtime runtime, Logger log, const MeasurementRegistry& registry, std::uint32_t periodMs = 5000u)
        : runtime_(runtime), log_(log), registry_(registry), periodMs_(periodMs) {}

    bool init() override;
    void run() override;
    void setPeriod(std::uint32_t periodMs) { periodMs_ = periodMs; }

private:
    Runtime runtime_;
    Logger log_;
    const MeasurementRegistry& registry_;
    std::uint32_t periodMs_{};
    std::uint32_t nextAt_{};
};

} // namespace slstm32::sensors
