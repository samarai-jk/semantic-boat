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

// Module inputs D10..D13, D20..D23 as wired (2026-10-24): PB4 (rpm), PB5
// (ignition), PB14 (starter solenoid), PC13 (fuel stop solenoid), PA0
// (gearbox reverse), PA1 (gearbox forward), PA3 (preheater relay), PA7
// (reserve 1). Inputs
// with pull-up, each on its own EXTI line (any of them can count pulses).
// The bindings report the physical pin level; the 12v-ad-input-iso module
// drives its output low while 12 V is present at its terminal, which the
// module driver (semantic_modules::AdInputIso12v) inverts. An unplugged input
// reads "no 12 V".
std::array<slstm32::drivers::DigitalInputHardware, digitalInputCount> digitalInputs();
inline constexpr const char* digitalInputNames[digitalInputCount]{
    "D10 rpm",           "D11 ignition",          "D12 starter solenoid", "D13 fuel stop solenoid",
    "D20 gearbox reverse", "D21 gearbox forward", "D22 preheater relay",  "D23 reserve 1"};
// The two 12v-ad-input-iso modules (I2C1): AD1 has the digital inputs
// D10..D13 (indices 0..3) and the analog inputs A10..A13, AD2 D20..D23
// (indices 4..7) and A20..A23.
inline constexpr std::uint8_t ad1Address = 0x49u; // R34 fitted
inline constexpr std::uint8_t ad2Address = 0x48u; // R33 fitted
inline constexpr std::size_t moduleAnalogCount = 8u;
inline constexpr const char* moduleAnalogNames[moduleAnalogCount]{
    "A10 oil pressure", "A11 coolant temperature", "A12 alternator voltage", "A13 starter voltage",
    "A20 12 V system",  "A21 reserve 1",           "A22 reserve 2",          "A23 reserve 3"};
// Indices of the gearbox reed switches (12 V while the gear is engaged).
inline constexpr std::size_t gearReverseInput = 4u;
inline constexpr std::size_t gearForwardInput = 5u;
// GPIO pin mask of a digital input, as passed to HAL_GPIO_EXTI_Callback().
std::uint16_t digitalInputMask(std::size_t index);

// DO0..DO2: TIM1 CH1..CH3 (PA8 preheater, PA9 starter, PA10 stop
// solenoid), PWM mode 1, active high, one shared period (1 kHz). Duty 0 =
// off, 1 = on. 10 kOhm pull-downs on the host keep them off during reset.
// Engine control is a separate sub-project: the firmware leaves them off.
std::array<slstm32::drivers::PwmOutputHardware, outputCount> outputs();

// Status LED: the green half of the enclosure LED (common anode on +3.3 V,
// the pin sinks the cathode, as host-dev's H9 on LT1) on PA15 (TIM2_CH1);
// 1 kHz PWM with low polarity, so duty = brightness. The red half is not
// connected.
slstm32::drivers::PwmOutputHardware greenLed();

// Buzzer: host-dev BUZ1 (passive, about 2.7 kHz resonance) through its
// low-side driver (input active high, 47 kOhm pull-down) on PA2, TIM15_CH1.
// A tone is a square wave; 50 % duty is full volume. Silent with the
// output low; never statically high (the coil would only draw current).
slstm32::drivers::ToneOutputHardware buzzer();

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
