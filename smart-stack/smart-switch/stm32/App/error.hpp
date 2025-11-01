#ifndef APP_ERROR_HPP
#define APP_ERROR_HPP

#include <cstdint>
#include "log.hpp"

namespace app {

enum class ErrorCode : uint16_t {
    None = 0,
    ButtonStuck = 10,
    SmbusError = 20,
    CanError = 30,
    OverTemp = 40,
    Unknown = 0xFFFF
};

enum class Severity : uint8_t { Info, Warning, Critical };

struct ErrorEvent {
    ErrorCode code;
    Severity severity;
    const char* msg;
};

class ErrorReporter {
public:
    static ErrorReporter& instance();
    void report(const ErrorEvent& ev);
    ErrorEvent last() const { return last_; }
private:
    ErrorEvent last_{ ErrorCode::None, Severity::Info, nullptr };
};

} // namespace app

#endif // APP_ERROR_HPP
