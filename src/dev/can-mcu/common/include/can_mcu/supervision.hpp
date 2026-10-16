#pragma once

#include <cstdint>

// Long-running supervision for can-mcu firmware: watchdog, supply monitor,
// and a crash record that survives the reset that follows a fatal error.
namespace can_mcu {

// Independent watchdog on the LSI clock. Once started it cannot be stopped
// until the next reset; refreshWatchdog() must then run more often than the
// timeout. The watchdog is frozen while a debugger halts the core.
void startWatchdog(std::uint32_t timeoutMs);
void refreshWatchdog();
bool watchdogRunning();

// Programmable voltage detector: an interrupt counts every fall of VDD below
// the threshold, including dips far shorter than an ADC conversion.
enum class PowerThreshold : std::uint8_t { v2_0, v2_2, v2_4, v2_5, v2_6, v2_8, v2_9 };
void startPowerMonitor(PowerThreshold threshold);
std::uint32_t powerDrops();
bool powerLow();

// Fault left by the previous run in the RTC backup registers. They keep
// their content across resets as long as the supply does not collapse
// completely. can_mcu_fatal_error() writes the record before waiting for the
// watchdog reset.
struct CrashRecord {
    enum class Kind : std::uint8_t { none, errorHandler, hardFault };
    Kind kind{Kind::none};
    std::uint32_t uptimeMs{};
    std::uint32_t cfsr{};  // configurable fault status
    std::uint32_t hfsr{};  // hard fault status
    std::uint32_t bfar{};  // bus fault address
    std::uint32_t mmfar{}; // memory management fault address
};
// Returns the record of the previous run and clears it.
CrashRecord takeCrashRecord();
// Used by can_mcu_fatal_error(); interrupts must be disabled.
void storeCrashRecord(CrashRecord::Kind kind);

} // namespace can_mcu
