#pragma once

#include <cstddef>
#include <cstdint>

namespace slstm32 {

enum class LogLevel : std::uint8_t { debug, info, warning, error };
using LogSink = void (*)(LogLevel, const char*, void*);

class Logger {
public:
    // Longest message produced by writef(), including the terminator. Longer
    // output is truncated. The buffer lives on the caller's stack.
    static constexpr std::size_t formatCapacity = 128u;

    Logger(LogSink sink = nullptr, void* context = nullptr) : sink_(sink), context_(context) {}
    void write(LogLevel level, const char* message) const {
        if (sink_) sink_(level, message, context_);
    }

    // printf-style convenience. With newlib-nano, floating-point conversions
    // are unavailable unless the firmware links with -u _printf_float.
    void writef(LogLevel level, const char* format, ...) const
        __attribute__((format(printf, 3, 4)));

private:
    LogSink sink_{};
    void* context_{};
};

} // namespace slstm32
