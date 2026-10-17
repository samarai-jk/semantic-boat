#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/hardware.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"

namespace slstm32::drivers {

// Converts an input (usually mV) into a value in thousandths of the
// channel's unit. Returns false for an input the characteristic cannot map.
struct AnalogConversion {
    bool (*convert)(const void* context, std::int32_t input, std::int32_t& value){};
    const void* context{};
};

// Straight line through two points, extrapolated outside them.
struct LinearCharacteristic {
    std::int32_t inputLow{};
    std::int32_t inputHigh{};
    std::int32_t valueLow{};
    std::int32_t valueHigh{};
};
AnalogConversion linearConversion(const LinearCharacteristic& characteristic);

struct AnalogSensorConfig {
    AnalogConversion conversion{};
    // An input outside this range is a sensor fault (broken wire, short,
    // missing sensor or supply): the channel fails with outOfRange.
    std::int32_t faultBelow{};
    std::int32_t faultAbove{};
    std::uint32_t periodMs{1000u};
};

// An analog sensor on an AnalogInputHardware binding (voltage transmitters,
// amplified RTDs and thermocouples, pressure senders): converts each input
// into the value channel. The optional input channel receives the input
// itself (Quantity::voltage), which helps when checking a sensor on the
// bench. Fine adjustment is the value channel's calibration.
class AnalogSensor final : public Driver {
public:
    AnalogSensor(Runtime runtime, AnalogInputHardware input, const AnalogSensorConfig& config,
                 sensors::Measurement* value, sensors::Identity identity, sensors::Measurement* inputChannel = nullptr)
        : runtime_(runtime), input_(input), config_(config), value_(value), identity_(identity),
          inputChannel_(inputChannel) {}

    bool init() override;
    void run() override;
    std::int32_t input() const { return lastInput_; }

private:
    Runtime runtime_;
    AnalogInputHardware input_;
    const AnalogSensorConfig& config_;
    sensors::Measurement* value_{};
    sensors::Identity identity_{};
    sensors::Measurement* inputChannel_{};
    std::uint32_t nextAt_{};
    std::int32_t lastInput_{};
};

} // namespace slstm32::drivers
