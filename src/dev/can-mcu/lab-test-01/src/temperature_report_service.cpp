#include "temperature_report_service.hpp"
#include "n2k/pgn.hpp"
#include "n2k/temperature.hpp"

namespace lab_test_01 {
namespace {

constexpr std::uint32_t reportPeriodMs = 2000u;
constexpr std::uint8_t temperaturePriority = 5u;
constexpr std::uint8_t boardInstance = 0u;
constexpr std::uint8_t mcuInstance = 1u;

} // namespace

bool TemperatureReportService::init() {
    nextReportAt_ = runtime_.millis() + reportPeriodMs;
    return true;
}

void TemperatureReportService::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextReportAt_) < 0) return;
    nextReportAt_ += reportPeriodMs;
    if (!node_.claimed()) return;
    if (board_.valid()) send(boardInstance, board_.milliCelsius());
    if (mcu_.temperatureValid()) send(mcuInstance, mcu_.dieMilliCelsius());
}

void TemperatureReportService::send(std::uint8_t instance, std::int32_t milliCelsius) {
    std::uint8_t data[8];
    n2k::encodeTemperatureExtendedRange(data, n2k::noSequenceId, instance,
                                        n2k::TemperatureSource::inside, milliCelsius);
    (void)node_.sendSingle(temperaturePriority, n2k::pgn::temperatureExtendedRange,
                           n2k::broadcastAddress, data, sizeof data);
}

} // namespace lab_test_01
