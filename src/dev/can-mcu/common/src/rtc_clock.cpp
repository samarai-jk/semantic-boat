#include "can_mcu/rtc_clock.hpp"
#include <cstdio>
#include <cstring>
extern "C" {
#include "main.h"
extern RTC_HandleTypeDef hrtc;
}

namespace can_mcu {

namespace {

// Days since 1970-01-01 of a civil date (H. Hinnant's algorithm).
std::int64_t daysFromCivil(std::int64_t year, unsigned month, unsigned day) {
    year -= month <= 2u ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const auto yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153u * (month + (month > 2u ? static_cast<unsigned>(-3) : 9u)) + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

bool inRange(const RtcClock::DateTime& time) {
    return time.year >= 2000u && time.year <= 2099u && time.month >= 1u && time.month <= 12u && time.day >= 1u &&
           time.day <= 31u && time.hour < 24u && time.minute < 60u && time.second < 60u;
}

void format(char (&out)[32], const RtcClock::DateTime& time) {
    std::snprintf(out, sizeof out, "%04u-%02u-%02u %02u:%02u:%02u", static_cast<unsigned>(time.year),
                  static_cast<unsigned>(time.month), static_cast<unsigned>(time.day), static_cast<unsigned>(time.hour),
                  static_cast<unsigned>(time.minute), static_cast<unsigned>(time.second));
}

bool lseRunning() { return (RCC->BDCR & RCC_BDCR_LSERDY) != 0u; }

} // namespace

bool RtcClock::valid() const {
    // INITS: the calendar has been initialized (year not 0), i.e. set once
    // and kept since the last backup domain reset.
    return (RTC->ISR & RTC_ISR_INITS) != 0u;
}

bool RtcClock::read(DateTime& time) const {
    if (!valid()) return false;
    RTC_TimeTypeDef clock{};
    RTC_DateTypeDef date{};
    // The date must be read after the time: that unlocks the shadow registers.
    if (HAL_RTC_GetTime(&hrtc, &clock, RTC_FORMAT_BIN) != HAL_OK) return false;
    if (HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK) return false;
    time = {static_cast<std::uint16_t>(2000u + date.Year), date.Month, date.Date, clock.Hours, clock.Minutes,
            clock.Seconds};
    return true;
}

bool RtcClock::set(const DateTime& time) {
    if (!inRange(time)) return false;
    RTC_TimeTypeDef clock{};
    clock.Hours = time.hour;
    clock.Minutes = time.minute;
    clock.Seconds = time.second;
    clock.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    clock.StoreOperation = RTC_STOREOPERATION_RESET;
    RTC_DateTypeDef date{};
    date.Year = static_cast<std::uint8_t>(time.year - 2000u);
    date.Month = time.month;
    date.Date = time.day;
    // 1970-01-01 was a Thursday; the RTC counts Monday = 1 .. Sunday = 7.
    date.WeekDay = static_cast<std::uint8_t>((daysFromCivil(time.year, time.month, time.day) + 3) % 7 + 1);
    return HAL_RTC_SetTime(&hrtc, &clock, RTC_FORMAT_BIN) == HAL_OK &&
           HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN) == HAL_OK;
}

std::int64_t RtcClock::toUnix(const DateTime& time) {
    return daysFromCivil(time.year, time.month, time.day) * 86400 + time.hour * 3600 + time.minute * 60 + time.second;
}

RtcClock::DateTime RtcClock::fromUnix(std::int64_t seconds) {
    std::int64_t days = seconds / 86400;
    std::int64_t rest = seconds % 86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    // Inverse of daysFromCivil.
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const auto doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned day = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned month = mp < 10u ? mp + 3u : mp - 9u;
    const std::int64_t year = static_cast<std::int64_t>(yoe) + era * 400 + (month <= 2u ? 1 : 0);
    return {static_cast<std::uint16_t>(year), static_cast<std::uint8_t>(month), static_cast<std::uint8_t>(day),
            static_cast<std::uint8_t>(rest / 3600), static_cast<std::uint8_t>(rest / 60 % 60),
            static_cast<std::uint8_t>(rest % 60)};
}

std::int64_t RtcClock::unixSeconds() const {
    DateTime time{};
    return read(time) ? toUnix(time) : -1;
}

bool RtcClock::setUnixSeconds(std::int64_t seconds) { return set(fromUnix(seconds)); }

bool RtcClock::parse(const char* text, DateTime& time) {
    // "YYYY-MM-DD HH:MM:SS" (or T between date and time); digits at fixed places.
    constexpr char pattern[] = "dddd-dd-dd?dd:dd:dd";
    unsigned fields[6]{};
    unsigned field = 0u;
    for (std::size_t index = 0u; index < sizeof pattern - 1u; ++index) {
        const char c = text[index];
        if (pattern[index] == 'd') {
            if (c < '0' || c > '9') return false;
            fields[field] = fields[field] * 10u + static_cast<unsigned>(c - '0');
        } else {
            if (pattern[index] == '?' ? (c != ' ' && c != 'T') : c != pattern[index]) return false;
            ++field;
        }
    }
    if (text[sizeof pattern - 1u] != '\0') return false;
    time = {static_cast<std::uint16_t>(fields[0]), static_cast<std::uint8_t>(fields[1]),
            static_cast<std::uint8_t>(fields[2]), static_cast<std::uint8_t>(fields[3]),
            static_cast<std::uint8_t>(fields[4]), static_cast<std::uint8_t>(fields[5])};
    return inRange(time);
}

void RtcClock::logState() const {
    using slstm32::LogLevel;
    if (!lseRunning()) {
        log_.write(LogLevel::error, "rtc: LSE crystal not running, the clock stands still");
        return;
    }
    DateTime time{};
    if (!read(time)) {
        log_.write(LogLevel::warning, "rtc: not set (new board, or power lost without a backup cell); set with 'rtc YYYY-MM-DD HH:MM:SS'");
        return;
    }
    char text[32];
    format(text, time);
    log_.writef(LogLevel::info, "rtc: %s UTC", text);
}

bool RtcClock::registerCommands(slstm32::CommandConsole& console) {
    return console.add({"rtc", "rtc [YYYY-MM-DD HH:MM:SS]  show or set the clock (UTC)", &rtcCommand, this});
}

void RtcClock::rtcCommand(void* context, std::size_t argc, const char* const* argv) {
    using slstm32::LogLevel;
    auto& self = *static_cast<RtcClock*>(context);
    if (argc == 3u || argc == 2u) {
        // The console splits "date time" into two arguments.
        char text[32]{};
        std::snprintf(text, sizeof text, argc == 3u ? "%s %s" : "%s", argv[1], argc == 3u ? argv[2] : "");
        DateTime time{};
        if (!parse(text, time)) {
            self.log_.write(LogLevel::error, "usage: rtc [YYYY-MM-DD HH:MM:SS] (UTC)");
            return;
        }
        if (!self.set(time)) {
            self.log_.write(LogLevel::error, "rtc: setting the clock failed");
            return;
        }
    } else if (argc != 1u) {
        self.log_.write(LogLevel::error, "usage: rtc [YYYY-MM-DD HH:MM:SS] (UTC)");
        return;
    }
    self.logState();
}

} // namespace can_mcu
