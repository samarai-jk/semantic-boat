#include "error.hpp"

namespace app {

ErrorReporter& ErrorReporter::instance() {
    static ErrorReporter r;
    return r;
}

void ErrorReporter::report(const ErrorEvent& ev) {
    last_ = ev;
    // Minimal formatted logging
    const char* sev = (ev.severity == Severity::Critical) ? "CRIT" : (ev.severity == Severity::Warning ? "WARN" : "INFO");
    if (ev.msg) {
        log_writef(LogLevel::Error, "[%s] err=%u %s", sev, static_cast<unsigned>(ev.code), ev.msg);
    } else {
        log_writef(LogLevel::Error, "[%s] err=%u", sev, static_cast<unsigned>(ev.code));
    }
}

} // namespace app
