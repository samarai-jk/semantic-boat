#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "can_mcu/one_wire_pin.hpp"
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/hardware.hpp"

// engine-mon host wiring (on the bench: host-dev). Pin map and bridges:
// ../README.md.
namespace engine_mon::board {

inline constexpr std::size_t buttonCount = 2u;
inline constexpr std::size_t digitalInputCount = 8u;
inline constexpr std::size_t outputCount = 3u;
inline constexpr std::size_t analogInputCount = 3u;

// BTN0, BTN1 (IDs 0, 1) on the frontends BTN_FUN_4 (PB10) and BTN_FUN_5
// (PB11): the buttons reachable from outside the case. Bridges R51, R52.
std::array<slstm32::drivers::ButtonConfig, buttonCount> buttons();

// DI0..DI7: PA0, PA1, PA3, PA7, PB4, PB5, PC13, PA15, inputs with pull-up,
// each on its own EXTI line (any of them can count pulses). Physical pin
// level: the 12v-ad-input-iso module drives low while 12 V is present, and
// an unplugged input reads high.
std::array<slstm32::drivers::DigitalInputHardware, digitalInputCount> digitalInputs();
// GPIO pin mask of a digital input, as passed to HAL_GPIO_EXTI_Callback().
std::uint16_t digitalInputMask(std::size_t index);

// DO0..DO2: TIM1 CH1..CH3 (PA8, PA9, PA10), PWM mode 1, active high, one
// shared period (1 kHz). Duty 0 = off, 1 = on. Low (off) during reset only
// if the host has pull-downs: the timer pins float until started.
std::array<slstm32::drivers::PwmOutputHardware, outputCount> outputs();

// AI0..AI2: PA4..PA6, ADC1 IN9..IN11, sampled by can_mcu::McuMonitor.
inline constexpr std::array<std::uint8_t, analogInputCount> analogChannels{9u, 10u, 11u};

// 1-Wire buses: OW0 on PB12, OW1 on PB2 (external 4.7 kOhm pull-ups).
inline constexpr can_mcu::GpioPort oneWire0Port = can_mcu::GpioPort::b;
inline constexpr std::uint8_t oneWire0Pin = 12u;
inline constexpr can_mcu::GpioPort oneWire1Port = can_mcu::GpioPort::b;
inline constexpr std::uint8_t oneWire1Pin = 2u;

} // namespace engine_mon::board
