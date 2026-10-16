#include "slstm32/log.hpp"
#include <cstdarg>
#include <cstdio>

namespace slstm32 {

void Logger::writef(LogLevel level, const char* format, ...) const {
    if (!sink_ || !format) return;
    char message[formatCapacity];
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(message, sizeof message, format, arguments);
    va_end(arguments);
    if (written < 0) return;
    sink_(level, message, context_);
}

} // namespace slstm32
