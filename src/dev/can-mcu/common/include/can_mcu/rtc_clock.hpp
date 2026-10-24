#pragma once

#include <cstdint>
#include "slstm32/console.hpp"
#include "slstm32/log.hpp"

namespace can_mcu {

// Calendar time (UTC) of the STM32L4 RTC, clocked by the 32.768 kHz LSE
// crystal. The RTC sits in the backup domain: it keeps running through
// resets, and through power loss only with a backup cell on CN6 (VBAT).
// Without the cell the backup domain resets at power-up and the time is
// "not set" until set again; so are the backup registers (crash record,
// bootloader flags).
//
// Needs the device's CubeMX RTC (`hrtc`, clock from LSE, no calendar set by
// CubeMX). Console: "rtc" shows the time, "rtc YYYY-MM-DD HH:MM:SS" sets it
// (UTC).
class RtcClock {
public:
    struct DateTime {
        std::uint16_t year{}; // 2000..2099
        std::uint8_t month{}; // 1..12
        std::uint8_t day{};   // 1..31
        std::uint8_t hour{};
        std::uint8_t minute{};
        std::uint8_t second{};
    };

    explicit RtcClock(slstm32::Logger log) : log_(log) {}

    // True once the calendar was set (and kept since).
    bool valid() const;
    bool read(DateTime& time) const;
    bool set(const DateTime& time);
    // Seconds since 1970-01-01 UTC, or -1 while not set.
    std::int64_t unixSeconds() const;
    bool setUnixSeconds(std::int64_t seconds);

    // Logs the state at startup: time kept, not set, or the LSE not running.
    void logState() const;
    bool registerCommands(slstm32::CommandConsole& console);

    static std::int64_t toUnix(const DateTime& time);
    static DateTime fromUnix(std::int64_t seconds);
    // "2026-10-24 15:30:12" or "2026-10-24T15:30:12"; false if malformed.
    static bool parse(const char* text, DateTime& time);

private:
    static void rtcCommand(void* context, std::size_t argc, const char* const* argv);

    slstm32::Logger log_;
};

} // namespace can_mcu
