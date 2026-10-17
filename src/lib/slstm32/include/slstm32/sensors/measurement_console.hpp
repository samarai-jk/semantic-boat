#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/console.hpp"
#include "slstm32/log.hpp"
#include "slstm32/sensors/registry.hpp"

namespace slstm32::sensors {

// Service commands for the measurement channels of a registry:
//   sensors                          list all channels
//   sensor <name>                    details, calibration, alarm rules
//   sensor <name> cal <reference>    offset calibration at a reference value
//   sensor <name> cal2 <reference>   second point: offset and gain from the
//                                    last "cal" point and this one
//   sensor <name> alarm <rule> <threshold>
//   sensor <name> reset              calibration and thresholds to defaults
//   sensor <name> stats              restart minimum and maximum
//   ack                              acknowledge all alarms
// Values are in the channel's unit (C, %, V), e.g. "sensor board cal 21.5".
class MeasurementConsole {
public:
    MeasurementConsole(MeasurementRegistry& registry, Logger log) : registry_(registry), log_(log) {}
    bool registerCommands(CommandConsole& console);

private:
    static void sensorsCommand(void* context, std::size_t argc, const char* const* argv);
    static void sensorCommand(void* context, std::size_t argc, const char* const* argv);
    static void ackCommand(void* context, std::size_t argc, const char* const* argv);
    void list() const;
    void detail(std::size_t index) const;
    void calibrate(std::size_t index, std::int32_t reference, bool secondPoint);
    void reportSaved(std::size_t index, bool saved) const;

    MeasurementRegistry& registry_;
    Logger log_;
    // First point of a two-point calibration.
    std::size_t pointChannel_{~std::size_t{}};
    std::int32_t pointRaw_{};
    std::int32_t pointReference_{};
};

} // namespace slstm32::sensors
