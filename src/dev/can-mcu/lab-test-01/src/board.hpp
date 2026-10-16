#pragma once

#include <array>
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/hardware.hpp"

// LT1 host wiring on host-dev. See ../README.md.
namespace lab_test_01::board {

inline constexpr std::size_t buttonCount = 6u;

// BF1..BF6 with IDs 0..5, wired to frontends BTN_FUN_0..BTN_FUN_5.
std::array<slstm32::drivers::ButtonConfig, buttonCount> buttons();

// TIM1 CH1 (PA8, green), CH2 (PA9, red), CH3 (PA10, buzzer). The three
// channels share one timer period: a tone changes the LED PWM frequency and
// the binding rescales the LED compare values to keep their duty.
slstm32::drivers::PwmOutputHardware greenLed();
slstm32::drivers::PwmOutputHardware redLed();
slstm32::drivers::ToneOutputHardware buzzer();

} // namespace lab_test_01::board
