#pragma once

#include <cstddef>
#include <cstdint>

// Register-level access to what the bootloader uses on the can-mcu: clock,
// millisecond tick, CAN, the yellow CAN LED, flash, RTC backup registers, the
// unique ID, and a polled log on the ST-Link virtual COM port. No HAL: the
// bootloader stays small and leaves no HAL state behind for the application.
namespace boot::hw {

struct Frame {
    std::uint32_t id{};   // 29-bit identifier
    std::uint8_t length{};
    std::uint8_t data[8]{};
};

// HSE (8 MHz crystal) as system clock, HSI16 if the crystal does not start.
// Returns the system clock in Hz.
std::uint32_t startClock();
void startTick(std::uint32_t systemClockHz);
std::uint32_t millis();
void delay(std::uint32_t ms);

// USART1 (PB6/PB7, ST-Link virtual COM port), 115200 8N1, transmit only.
void startLog(std::uint32_t systemClockHz);
void log(const char* text);
void logHex(const char* text, std::uint32_t value);

// CAN1 on PA11/PA12 at 250 kbit/s, transceiver in high-speed mode (PB1 low),
// every frame accepted into FIFO 0.
bool startCan(std::uint32_t systemClockHz);
bool canSend(const Frame& frame); // false if no mailbox frees up within 5 ms
bool canReceive(Frame& frame);
void setLed(bool on);             // yellow RJ45 LED, PB0 active low

// Flash (2 KB pages; addresses absolute).
bool flashErasePage(std::uint32_t address);
bool flashProgram(std::uint32_t address, const std::uint8_t* data, std::size_t size); // size multiple of 8
void flashFlushCaches();

// RTC backup registers (they survive a reset).
std::uint32_t backupRegister(unsigned index);
void setBackupRegister(unsigned index, std::uint32_t value);

void uniqueId(std::uint8_t (&out)[12]);

// Puts clock and the peripherals used back into their reset state, sets the
// vector table, and starts the application. Does not return.
[[noreturn]] void startApplication(std::uint32_t base);
[[noreturn]] void reset();

} // namespace boot::hw
