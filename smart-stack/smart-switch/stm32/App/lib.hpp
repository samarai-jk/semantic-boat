#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Log levels for tracing
typedef enum log_level_t {
    LOG_INFO = 0,
    LOG_VERBOSE,
    LOG_ERROR,
    LOG_FATAL,
    LOG_WARNING,
    LOG_BUGFIX
} log_level_t;

// Trace a line with level; implementation also adds timestamp and newline
void swo_trace_line_level(log_level_t level, const char *s);

// Trace a formatted line with level and variable arguments
void swo_trace_linef(log_level_t level, const char *fmt, ...);

// Backward compatible helper: defaults to INFO
static inline void swo_trace_line(const char *s) { swo_trace_line_level(LOG_INFO, s); }

#ifdef __cplusplus
} // extern "C"

// C++-friendly overloads that accept std::string
#include <string>
inline void swo_trace_line(log_level_t level, const std::string &s) {
    swo_trace_line_level(level, s.c_str());
}
inline void swo_trace_line(const std::string &s) {
    swo_trace_line_level(LOG_INFO, s.c_str());
}
#endif
