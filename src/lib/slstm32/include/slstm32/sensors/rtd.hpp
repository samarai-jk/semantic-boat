#pragma once

#include <cstdint>
#include "slstm32/drivers/analog_sensor.hpp"

// Platinum resistance thermometers (PT100, PT1000) per IEC 60751.
namespace slstm32::sensors {

// Temperature of a platinum RTD from its resistance, in m°C. Inverts the
// Callendar-Van Dusen equation for 0..850 C (exact there; below 0 C the
// error stays under 0.1 C down to -50 C). r0 is the resistance at 0 C
// (100 for PT100, 1000 for PT1000).
std::int32_t rtdMilliCelsius(float ohms, float r0 = 100.0f);

// RTD in a bridge with a differential amplifier, as on most analog RTD
// modules: R = (a * V + b) / (c - V), V the output in volts.
// DFRobot SEN0198 (PT100, from DFRobot's library): a = 1800, b = 3976.2
// (220.9 * 18), c = 39.762 (2.209 * 18).
struct RtdBridge {
    float a{};
    float b{};
    float c{};
    float r0{100.0f};
};

// AnalogSensor conversion from mV to m°C through the bridge and the RTD.
drivers::AnalogConversion rtdBridgeConversion(const RtdBridge& bridge);

} // namespace slstm32::sensors
