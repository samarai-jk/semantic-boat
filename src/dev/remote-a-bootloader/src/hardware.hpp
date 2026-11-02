#pragma once

#include <cstddef>
#include <cstdint>

// Register-level access to what the bootloader uses on remote-a: clock,
// millisecond tick, the RS485 port, the signal LED, the EEPROM, flash, RTC
// backup registers, and a polled log on the ST-Link virtual COM port. No HAL:
// the bootloader stays small and leaves no HAL state behind for the
// application.
namespace boot::hw {

// HSE (8 MHz crystal) as system clock, HSI16 if the crystal does not start.
// Returns the system clock in Hz.
std::uint32_t startClock();
void startTick(std::uint32_t systemClockHz);
std::uint32_t millis();

// USART1 (PA9, ST-Link virtual COM port), 115200 8N1, transmit only.
void startLog(std::uint32_t systemClockHz);
void log(const char* text);
void logHex(const char* text, std::uint32_t value);

// USART2 on the RS485 transceiver (PA2 TX, PA3 RX, PA1 driver enable by the
// USART), 115200 8N1, polled.
void startRs485(std::uint32_t systemClockHz);
bool rs485Read(std::uint8_t& byte);
void rs485Write(const std::uint8_t* data, std::size_t size); // returns once the last bit has left

void startLed();
void setLed(bool on); // SIG_LED_0, PB12, active high

// AT24C256 at 0x52 on I2C2 (PB13 SCL, PB14 SDA), 100 kHz; size <= 255.
void startEeprom(std::uint32_t systemClockHz);
bool eepromRead(std::uint16_t address, std::uint8_t* data, std::size_t size);

// Flash (2 KB pages; addresses absolute).
bool flashErasePage(std::uint32_t address);
bool flashProgram(std::uint32_t address, const std::uint8_t* data, std::size_t size); // size multiple of 8
void flashFlushCaches();

// RTC backup registers (they survive a reset).
std::uint32_t backupRegister(unsigned index);
void setBackupRegister(unsigned index, std::uint32_t value);

// Puts clock and the peripherals used back into their reset state (clock
// enables exactly as at reset, saved by startClock()), sets the vector table,
// and starts the application. Does not return.
[[noreturn]] void startApplication(std::uint32_t base);
[[noreturn]] void reset();

} // namespace boot::hw
