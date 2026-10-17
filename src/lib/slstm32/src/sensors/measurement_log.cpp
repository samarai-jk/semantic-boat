#include "slstm32/sensors/measurement_log.hpp"
#include <cstdio>

namespace slstm32::sensors {

namespace {
constexpr std::size_t lineLimit = 118u; // with "values: " just below Logger::formatCapacity
} // namespace

bool MeasurementLog::init() {
    nextAt_ = runtime_.millis() + periodMs_;
    return true;
}

void MeasurementLog::run() {
    const auto now = runtime_.millis();
    if (periodMs_ == 0u || static_cast<std::int32_t>(now - nextAt_) < 0) return;
    nextAt_ += periodMs_;
    if (static_cast<std::int32_t>(now - nextAt_) >= 0) nextAt_ = now + periodMs_;
    char line[lineLimit + 1u];
    std::size_t length = 0u;
    for (std::size_t index = 0u; index < registry_.size(); ++index) {
        const auto& channel = registry_.at(index);
        char entry[48];
        char value[20];
        if (channel.valid()) {
            std::snprintf(entry, sizeof entry, "%s %s %s", channel.name(), formatMilli(value, channel.value()),
                          unitName(channel.quantity()));
        } else {
            std::snprintf(entry, sizeof entry, "%s %s", channel.name(),
                          channel.status() == Status::fault ? faultName(channel.fault()) : statusName(channel.status()));
        }
        std::size_t entryLength = 0u;
        while (entry[entryLength] != '\0') ++entryLength;
        if (length != 0u && length + 3u + entryLength > lineLimit) {
            log_.writef(LogLevel::info, "values: %s", line);
            length = 0u;
        }
        length += static_cast<std::size_t>(
            std::snprintf(line + length, sizeof line - length, "%s%s", length ? " | " : "", entry));
        if (length > lineLimit) length = lineLimit;
    }
    if (length != 0u) log_.writef(LogLevel::info, "values: %s", line);
}

} // namespace slstm32::sensors
