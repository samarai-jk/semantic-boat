#include "diagnostics_service.hpp"
#include <cstdio>
#include <cstdlib>

namespace lab_test_01 {
namespace {

using slstm32::LogLevel;

constexpr std::uint32_t heartbeatPeriodMs = 10000u;
constexpr std::uint32_t supplyCheckPeriodMs = 100u;
constexpr std::uint16_t supplyDipThresholdMv = 3150u;

bool due(std::uint32_t now, std::uint32_t deadline) {
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

// Formats milli-degrees as "23.06" (two decimals) or "--" when invalid,
// without floating-point printf support.
const char* formatCelsius(char (&buffer)[12], bool valid, std::int32_t milliCelsius) {
    if (!valid) return "--";
    const auto centi = (milliCelsius >= 0 ? milliCelsius + 5 : milliCelsius - 5) / 10;
    const auto magnitude = std::labs(static_cast<long>(centi));
    std::snprintf(buffer, sizeof buffer, "%s%ld.%02ld", centi < 0 ? "-" : "", magnitude / 100,
                  magnitude % 100);
    return buffer;
}

} // namespace

bool DiagnosticsService::init() {
    const auto now = runtime_.millis();
    nextSupplyCheckAt_ = now + supplyCheckPeriodMs;
    nextHeartbeatAt_ = now + heartbeatPeriodMs;
    return true;
}

void DiagnosticsService::run() {
    const auto now = runtime_.millis();
    checkSupply(now);
    heartbeat(now);
}

void DiagnosticsService::checkSupply(std::uint32_t now) {
    if (!due(now, nextSupplyCheckAt_)) return;
    nextSupplyCheckAt_ = now + supplyCheckPeriodMs;
    const auto window = mcu_.takeWindow();
    if (window.samples == 0u) return;
    if (window.minimumMv < minimumMv_) minimumMv_ = window.minimumMv;
    if (window.maximumMv > maximumMv_) maximumMv_ = window.maximumMv;
    if (window.minimumMv < supplyDipThresholdMv) {
        log_.writef(LogLevel::warning, "supply dip: VDDA %u..%u mV in last %lu ms%s",
                    static_cast<unsigned>(window.minimumMv), static_cast<unsigned>(window.maximumMv),
                    static_cast<unsigned long>(supplyCheckPeriodMs),
                    buzzer_.playing() ? " (tone playing)" : "");
    }
}

void DiagnosticsService::heartbeat(std::uint32_t now) {
    if (!due(now, nextHeartbeatAt_)) return;
    nextHeartbeatAt_ += heartbeatPeriodMs;
    char board[12];
    char die[12];
    log_.writef(LogLevel::debug,
                "heartbeat: up %lu s, VDDA %u..%u mV, board %s C, mcu %s C, i2c err %lu rec %lu, "
                "ev drop %lu",
                static_cast<unsigned long>(now / 1000u), static_cast<unsigned>(minimumMv_),
                static_cast<unsigned>(maximumMv_),
                formatCelsius(board, boardTemperature_.valid(), boardTemperature_.milliCelsius()),
                formatCelsius(die, mcu_.temperatureValid(), mcu_.dieMilliCelsius()),
                static_cast<unsigned long>(i2c_.errors()),
                static_cast<unsigned long>(i2c_.recoveries()),
                static_cast<unsigned long>(events_.droppedEvents()));
    minimumMv_ = 0xffffu;
    maximumMv_ = 0u;
}

} // namespace lab_test_01
