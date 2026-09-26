#pragma once

#include <cstdint>

namespace slstm32 {

enum class LogLevel : std::uint8_t { debug, info, warning, error };
using LogSink = void (*)(LogLevel, const char*, void*);

class Logger {
public:
    Logger(LogSink sink = nullptr, void* context = nullptr) : sink_(sink), context_(context) {}
    void write(LogLevel level, const char* message) const {
        if (sink_) sink_(level, message, context_);
    }

private:
    LogSink sink_{};
    void* context_{};
};

} // namespace slstm32
