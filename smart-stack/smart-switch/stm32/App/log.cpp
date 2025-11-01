#include "log.hpp"
#include <cstdarg>

extern "C" {
#include "main.h"
}

namespace app {

static inline bool swo_enabled() {
    return (ITM->TCR & ITM_TCR_ITMENA_Msk) && (ITM->TER & 1);
}

void log_init() {
    // Assume debugger config enables SWO; nothing to do here.
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

void log_write(LogLevel level, const char* msg) {
    (void)level; // could prefix with level tag if desired
    swo_write(msg);
    swo_putc('\n');
}

void log_writef(LogLevel level, const char* fmt, ...) {
    (void)level;
    if (!swo_enabled()) return;
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    // Minimal vsnprintf-free formatting to keep footprint; rely on truncation
    int i = 0;
    for (const char* p = fmt; *p && i < (int)sizeof(buf)-1; ++p) {
        if (*p == '%' && *(p+1) == 's') {
            p += 1; const char* s = va_arg(ap, const char*);
            while (s && *s && i < (int)sizeof(buf)-1) buf[i++] = *s++;
        } else if (*p == '%' && (*(p+1) == 'd' || *(p+1) == 'u')) {
            bool sign = (*(p+1) == 'd'); p += 1; int v = va_arg(ap, int);
            unsigned int u = sign ? (unsigned int)((v < 0) ? -v : v) : (unsigned int)v;
            char tmp[16]; int ti = 0; do { tmp[ti++] = '0' + (u % 10); u /= 10; } while (u && ti < 16);
            if (sign && v < 0 && i < (int)sizeof(buf)-1) buf[i++] = '-';
            while (ti && i < (int)sizeof(buf)-1) buf[i++] = tmp[--ti];
        } else {
            buf[i++] = *p;
        }
    }
    buf[i] = 0;
    va_end(ap);
    swo_write(buf);
    swo_putc('\n');
}

} // namespace app
