#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "can_mcu/one_wire_pin.hpp"
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/hardware.hpp"

// engine-mon host wiring. Pin map, bridges, and CubeMX setup:
// ../ENGINE-SETUP.md.
namespace engine_mon::board {

inline constexpr std::size_t buttonCount = 2u;
inline constexpr std::size_t digitalInputCount = 8u;
inline constexpr std::size_t outputCount = 3u;
inline constexpr std::size_t analogInputCount = 3u;

// BF5, BF6 (IDs 0, 1; named like the host-dev buttons on the same
// frontends) on BTN_FUN_4 (PB10) and BTN_FUN_5
// (PB11): the buttons reachable from outside the case. Bridges R51, R52.
std::array<slstm32::drivers::ButtonConfig, buttonCount> buttons();
inline constexpr const char* buttonNames[buttonCount]{"BF5", "BF6"};

// DI0..DI7 = module inputs D10..D13, D20..D23: PA0 (rpm), PA1 (ignition),
// PA3 (starter solenoid), PA7 (fuel stop solenoid), PB4 (gearbox reverse),
// PB5 (gearbox forward), PB14 (preheater relay), PC13 (reserve 1). Inputs
// with pull-up, each on its own EXTI line (any of them can count pulses).
// Physical pin level: the 12v-ad-input-iso module drives low while 12 V is
// present, and an unplugged input reads high.
std::array<slstm32::drivers::DigitalInputHardware, digitalInputCount> digitalInputs();
// GPIO pin mask of a digital input, as passed to HAL_GPIO_EXTI_Callback().
std::uint16_t digitalInputMask(std::size_t index);

// DO0..DO2: TIM1 CH1..CH3 (PA8 preheater, PA9 starter, PA10 stop
// solenoid), PWM mode 1, active high, one shared period (1 kHz). Duty 0 =
// off, 1 = on. 10 kOhm pull-downs on the host keep them off during reset.
// Engine control is a separate sub-project: the firmware leaves them off.
std::array<slstm32::drivers::PwmOutputHardware, outputCount> outputs();

// Enclosure LEDs on TIM2 (1 kHz PWM, active high): green on PA15 (CH1),
// red on PA2 (CH3). Duty = brightness.
slstm32::drivers::PwmOutputHardware greenLed();
slstm32::drivers::PwmOutputHardware redLed();

// AI0..AI2: PA4..PA6, ADC1 IN9..IN11 (SEN0198 PT100 amplifiers: wet
// exhaust, alternator, heat exchanger), sampled by can_mcu::McuMonitor.
inline constexpr std::array<std::uint8_t, analogInputCount> analogChannels{9u, 10u, 11u};

// 1-Wire buses, one sensor each (external 4.7 kOhm pull-ups): OW0 on PB12
// (CT1780, dry exhaust), OW1 on PB13 (DS18B20, gearbox oil), OW2 on PB2
// (DS18B20, calorifier).
inline constexpr can_mcu::GpioPort oneWire0Port = can_mcu::GpioPort::b;
inline constexpr std::uint8_t oneWire0Pin = 12u;
inline constexpr can_mcu::GpioPort oneWire1Port = can_mcu::GpioPort::b;
inline constexpr std::uint8_t oneWire1Pin = 13u;
inline constexpr can_mcu::GpioPort oneWire2Port = can_mcu::GpioPort::b;
inline constexpr std::uint8_t oneWire2Pin = 2u;

} // namespace engine_mon::board
