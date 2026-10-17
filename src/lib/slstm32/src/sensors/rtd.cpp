#include "slstm32/sensors/rtd.hpp"
#include <cmath>

namespace slstm32::sensors {

namespace {

// IEC 60751 coefficients.
constexpr float coefficientA = 3.9083e-3f;
constexpr float coefficientB = -5.775e-7f;

bool convertBridge(const void* context, std::int32_t millivolts, std::int32_t& milliCelsius) {
    const auto& bridge = *static_cast<const RtdBridge*>(context);
    const float volts = static_cast<float>(millivolts) / 1000.0f;
    const float denominator = bridge.c - volts;
    if (denominator <= 0.0f) return false;
    const float ohms = (bridge.a * volts + bridge.b) / denominator;
    if (ohms <= 0.0f) return false;
    milliCelsius = rtdMilliCelsius(ohms, bridge.r0);
    return true;
}

} // namespace

std::int32_t rtdMilliCelsius(float ohms, float r0) {
    // R = R0 (1 + A t + B t^2), solved for t.
    const float ratio = ohms / r0;
    const float discriminant = coefficientA * coefficientA - 4.0f * coefficientB * (1.0f - ratio);
    if (discriminant < 0.0f) return 850000; // above the equation's range
    const float celsius = (-coefficientA + std::sqrt(discriminant)) / (2.0f * coefficientB);
    return static_cast<std::int32_t>(std::lround(celsius * 1000.0f));
}

drivers::AnalogConversion rtdBridgeConversion(const RtdBridge& bridge) {
    return {&convertBridge, &bridge};
}

} // namespace slstm32::sensors
