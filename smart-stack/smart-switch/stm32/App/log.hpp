#ifndef APP_LOG_HPP
#define APP_LOG_HPP

#include <cstdint>
#include <string>

namespace app {

enum class LogLevel : uint8_t { Debug, Info, Warn, Error };

void log_init();
void log_write(LogLevel level, const char* msg);
void log_write(LogLevel level, const std::string& msg);
void log_writef(LogLevel level, const char* fmt, ...);

} // namespace app

#endif // APP_LOG_HPP
