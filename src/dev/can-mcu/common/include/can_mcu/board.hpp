#pragma once

#include <cstdint>
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/hardware.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"

// Board support shared by every firmware built on the can-mcu board. It binds
// the hardware that is fixed on the can-mcu itself (MCU core, ST-Link VCP,
// button frontends, ...) to slstm32. Host-specific bindings stay in the
// device directory. See ../../can-mcu.md for the hardware.
//
// Requirements on the device's CubeMX project:
// - USART1 on PB6/PB7 initialized (ST-Link VCP); the log output polls it.
// - EXTI interrupts must not be enabled in CubeMX: this layer owns all EXTI
//   vectors and forwards every line to HAL_GPIO_EXTI_Callback().
namespace can_mcu {

// Call first in the device's application init, before any logging.
void init();

slstm32::Runtime runtime();

// Busy-wait based on the DWT cycle counter. Intended for short protocol
// timing (microseconds), never for application delays.
void delayMicroseconds(std::uint32_t microseconds);

// The DWT cycle counter as a fine time base (wraps every 53 s at 80 MHz).
// Every can-mcu device runs its core at coreClockHz.
inline constexpr std::uint32_t coreClockHz = 80000000u;
std::uint32_t cycleCounter();

// Non-blocking transmitter on the ST-Link virtual COM port (USART1).
slstm32::drivers::SerialOutputHardware vcpOutput();

// Receiver on the ST-Link virtual COM port: the USART1 receive interrupt
// fills a 64-byte ring that read() drains. This layer owns USART1_IRQHandler;
// do not enable the USART1 interrupt in CubeMX.
slstm32::drivers::SerialInputHardware vcpInput();

enum class ButtonFrontend : std::uint8_t { fun0, fun1, fun2, fun3, fun4, fun5, power };

// Interrupt-driven, active-low button on one of the seven frontends. The
// frontend's 0R bridge must be fitted. The returned interrupt token is the
// GPIO pin mask passed to HAL_GPIO_EXTI_Callback().
slstm32::drivers::ButtonConfig buttonFrontend(ButtonFrontend frontend, std::uint8_t id);

enum class ResetCause : std::uint8_t {
    powerOn, // power-on or brown-out
    pin,     // NRST: reset button or debug probe
    software,
    independentWatchdog,
    windowWatchdog,
    lowPower,
    other,
};

const char* resetCause();
ResetCause resetCauseCode();

// The MCU's 96-bit factory unique ID, little-endian.
void uniqueId(std::uint8_t (&out)[12]);

// Writes reset cause, clocks, and MCU identity to the log.
void logBoardInfo(const slstm32::Logger& log);

} // namespace can_mcu
