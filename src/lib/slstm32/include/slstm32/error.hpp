#pragma once

#include <cstdint>
#include "slstm32/log.hpp"

namespace slstm32 {

enum class ErrorSeverity : std::uint8_t { info, warning, critical };
struct Error {
    std::uint16_t code{};
    ErrorSeverity severity{ErrorSeverity::info};
    const char* message{};
};

class ErrorReporter {
public:
    explicit ErrorReporter(Logger logger = {}) : logger_(logger) {}
    void report(Error error) {
        last_ = error;
        logger_.write(error.severity == ErrorSeverity::info ? LogLevel::info : LogLevel::error,
                      error.message ? error.message : "unspecified error");
    }
    const Error& last() const { return last_; }

private:
    Logger logger_{};
    Error last_{};
};

} // namespace slstm32
