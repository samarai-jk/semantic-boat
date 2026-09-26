#pragma once

#include <array>
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/hardware.hpp"

namespace remote_a::hardware {

std::array<slstm32::drivers::ButtonConfig, 6> buttons();
slstm32::drivers::DigitalOutputHardware signalLed();
std::array<slstm32::drivers::PwmOutputHardware, 3> rgbLed();
slstm32::drivers::ToneOutputHardware buzzer();

} // namespace remote_a::hardware
