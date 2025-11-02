#include "log.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "main.h"
}

namespace app {

static inline bool swo_enabled() {
    return (ITM->TCR & ITM_TCR_ITMENA_Msk) && (ITM->TER & 1);
}

void log_init() {
    DBGMCU->CR |= DBGMCU_CR_TRACE_IOEN;
}

static void swo_putc(char c) {
    if (!swo_enabled()) return;
    while (ITM->PORT[0].u32 == 0) { __NOP(); }
    ITM->PORT[0].u8 = static_cast<uint8_t>(c);
}

static void swo_write(const char* s) {
    if (!swo_enabled()) return;
    while (*s) {
        swo_putc(*s++);
    }
}

static const char* level_to_str(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        default:              return "INFO";
    }
}

// Format and write: hhMMss.fff [LEVEL] Message\n
static void write_prefixed(LogLevel level, const char* msg) {
    if (!swo_enabled()) return;
    uint32_t ms = HAL_GetTick();
    unsigned long hh = (ms / 3600000UL) % 100UL;
    unsigned long MM = (ms / 60000UL)   % 60UL;
    unsigned long ss = (ms / 1000UL)    % 60UL;
    unsigned long fff = ms % 1000UL;

    char header[32];
    int n = std::snprintf(header, sizeof(header), "%02lu%02lu%02lu.%03lu [%s] ",
                          hh, MM, ss, fff, level_to_str(level));
    if (n > 0) {
        swo_write(header);
    }
    if (msg && *msg) {
        swo_write(msg);
    }
    swo_putc('\n');
}

void log_write(LogLevel level, const char* msg) {
    write_prefixed(level, msg);
}

void log_write(LogLevel level, const std::string& msg) {
    write_prefixed(level, msg.c_str());
}

void log_writef(LogLevel level, const char* fmt, ...) {
    if (!swo_enabled()) return;
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write_prefixed(level, buf);
}

} // namespace app
