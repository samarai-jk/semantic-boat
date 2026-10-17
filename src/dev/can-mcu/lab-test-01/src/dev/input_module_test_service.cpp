// DEVELOPMENT AND TEST CODE: compiled only with SB_DEV_TOOLS (AGENTS.md,
// "Firmware code categories and build profiles"). Never reference this from
// product code.
#include "input_module_test_service.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace lab_test_01 {
namespace {

using slstm32::LogLevel;

// Long enough to keep overnight logs readable; changes are logged at once.
constexpr std::uint32_t reportPeriodMs = 10000u;

bool due(std::uint32_t now, std::uint32_t deadline) {
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

} // namespace

bool InputModuleTestService::init() {
    const auto now = runtime_.millis();
    nextReportAt_ = now + reportPeriodMs;
    windowStartedAt_ = now;
    resetWindow();
    adcPresent_ = module_.adcPresent();
    log_.writef(LogLevel::info, "input module: ADC 0x%02X %s", static_cast<unsigned>(module_.adcAddress()),
                adcPresent_ ? "present" : "not answering");
    for (std::size_t channel = 0u; channel < channels; ++channel) {
        digital_[channel] = module_.digitalActive(channel);
        lastSamples_[channel] = module_.analogSamples(channel);
    }
    log_.writef(LogLevel::info, "input module: DI %u%u%u%u at start (1 = 12 V present)",
                static_cast<unsigned>(digital_[0]), static_cast<unsigned>(digital_[1]),
                static_cast<unsigned>(digital_[2]), static_cast<unsigned>(digital_[3]));
    return true;
}

void InputModuleTestService::run() {
    trackAnalog();
    watchDigital();
    report(runtime_.millis());
}

void InputModuleTestService::resetWindow() {
    windowSamples_ = {};
    minimumMv_.fill(std::numeric_limits<std::int32_t>::max());
    maximumMv_.fill(std::numeric_limits<std::int32_t>::min());
}

void InputModuleTestService::trackAnalog() {
    if (module_.adcPresent() != adcPresent_) {
        adcPresent_ = module_.adcPresent();
        log_.writef(adcPresent_ ? LogLevel::info : LogLevel::warning, "input module: ADC %s (errors %lu)",
                    adcPresent_ ? "appeared" : "disappeared",
                    static_cast<unsigned long>(module_.adcErrors()));
    }
    for (std::size_t channel = 0u; channel < channels; ++channel) {
        const auto samples = module_.analogSamples(channel);
        if (samples == lastSamples_[channel] || !module_.analogValid(channel)) continue;
        windowSamples_[channel] += samples - lastSamples_[channel];
        lastSamples_[channel] = samples;
        const auto millivolts = module_.analogMillivolts(channel);
        if (millivolts < minimumMv_[channel]) minimumMv_[channel] = millivolts;
        if (millivolts > maximumMv_[channel]) maximumMv_[channel] = millivolts;
    }
}

void InputModuleTestService::watchDigital() {
    for (std::size_t channel = 0u; channel < channels; ++channel) {
        const bool active = module_.digitalActive(channel);
        if (active == digital_[channel]) continue;
        digital_[channel] = active;
        log_.writef(LogLevel::info, "input module: DI%u %s (change #%lu)", static_cast<unsigned>(channel),
                    active ? "ON, 12 V present" : "off", static_cast<unsigned long>(module_.digitalChanges(channel)));
    }
}

void InputModuleTestService::report(std::uint32_t now) {
    if (!due(now, nextReportAt_)) return;
    nextReportAt_ += reportPeriodMs;
    if (!module_.adcPresent()) {
        resetWindow();
        return;
    }
    // "AI0 12.034 V pp 3 mV" per channel: last value and peak-to-peak noise.
    char text[4][28]{};
    for (std::size_t channel = 0u; channel < channels; ++channel) {
        if (windowSamples_[channel] == 0u) {
            std::snprintf(text[channel], sizeof text[channel], "AI%u --", static_cast<unsigned>(channel));
            continue;
        }
        const auto millivolts = module_.analogMillivolts(channel);
        const auto magnitude = std::labs(static_cast<long>(millivolts));
        std::snprintf(text[channel], sizeof text[channel], "AI%u %s%ld.%03ld V pp %ld",
                      static_cast<unsigned>(channel), millivolts < 0 ? "-" : "", magnitude / 1000,
                      magnitude % 1000, static_cast<long>(maximumMv_[channel] - minimumMv_[channel]));
    }
    log_.writef(LogLevel::info, "%s | %s | %s | %s mV", text[0], text[1], text[2], text[3]);
    const auto windowMs = now - windowStartedAt_;
    log_.writef(LogLevel::debug, "input module: DI %u%u%u%u, %lu samples/s per channel, ADC errors %lu",
                static_cast<unsigned>(digital_[0]), static_cast<unsigned>(digital_[1]),
                static_cast<unsigned>(digital_[2]), static_cast<unsigned>(digital_[3]),
                static_cast<unsigned long>(windowMs ? windowSamples_[0] * 1000u / windowMs : 0u),
                static_cast<unsigned long>(module_.adcErrors()));
    windowStartedAt_ = now;
    resetWindow();
}

} // namespace lab_test_01
