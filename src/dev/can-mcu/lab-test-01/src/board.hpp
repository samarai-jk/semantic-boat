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

// PA5..PA7 wired to OUT_D_1..3 of the 12v-ad-input-iso module (physical pin
// level, pull-up so an unplugged module reads "no 12 V"). PA4 (OUT_D_0) is
// the 1-Wire bus of the sensor tests, so DI0 is not connected.
std::array<slstm32::drivers::DigitalInputHardware, 4> isolatedModuleInputs();

} // namespace lab_test_01::board
