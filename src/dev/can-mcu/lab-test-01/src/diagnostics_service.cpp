#include "diagnostics_service.hpp"
#include <cstdio>
#include <cstdlib>

namespace lab_test_01 {
namespace {

using slstm32::LogLevel;

constexpr std::uint32_t heartbeatPeriodMs = 10000u;
constexpr std::uint32_t supplyCheckPeriodMs = 100u;
constexpr std::uint16_t minimumThresholdMv = 1800u;
constexpr std::uint16_t maximumThresholdMv = 3600u;

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
    if (window.minimumMv < dipThresholdMv_) {
        log_.writef(LogLevel::warning, "supply dip: VDDA %u..%u mV in last %lu ms%s",
                    static_cast<unsigned>(window.minimumMv), static_cast<unsigned>(window.maximumMv),
                    static_cast<unsigned long>(supplyCheckPeriodMs),
                    buzzer_.playing() ? " (tone playing)" : "");
    }
}

bool DiagnosticsService::registerCommands(slstm32::CommandConsole& console) {
    return console.add({"supply", "supply [<mV>]  supply voltage, set the dip threshold", &supplyCommand, this});
}

void DiagnosticsService::supplyCommand(void* context, std::size_t argc, const char* const* argv) {
    auto& self = *static_cast<DiagnosticsService*>(context);
    if (argc >= 2u) {
        char* end = nullptr;
        const auto value = std::strtoul(argv[1], &end, 10);
        if (end == argv[1] || *end != '\0' || value < minimumThresholdMv || value > maximumThresholdMv) {
            self.log_.writef(LogLevel::warning, "supply: threshold must be %u..%u mV",
                             static_cast<unsigned>(minimumThresholdMv), static_cast<unsigned>(maximumThresholdMv));
            return;
        }
        self.dipThresholdMv_ = static_cast<std::uint16_t>(value);
    }
    self.log_.writef(LogLevel::info, "supply: VDDA %u mV now, %u..%u mV since heartbeat, dip threshold %u mV",
                     static_cast<unsigned>(self.mcu_.supplyMv()), static_cast<unsigned>(self.minimumMv_),
                     static_cast<unsigned>(self.maximumMv_), static_cast<unsigned>(self.dipThresholdMv_));
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
