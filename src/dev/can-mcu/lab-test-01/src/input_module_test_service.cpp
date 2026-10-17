#include "input_module_test_service.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace lab_test_01 {
namespace {

using slstm32::LogLevel;

// Long enough to keep overnight logs readable; changes are logged at once.
constexpr std::uint32_t reportPeriodMs = 10000u;
constexpr std::uint32_t watchPeriodMs = 1000u;

// "1", "1.5", "0.25" volts to millivolts.
bool parseVolts(const char* text, std::int32_t& millivolts) {
    std::int32_t whole = 0;
    if (*text < '0' || *text > '9') return false;
    while (*text >= '0' && *text <= '9') {
        whole = whole * 10 + (*text++ - '0');
        if (whole > 100) return false;
    }
    std::int32_t fraction = 0;
    int digits = 0;
    if (*text == '.') {
        ++text;
        while (*text >= '0' && *text <= '9') {
            if (digits < 3) {
                fraction = fraction * 10 + (*text - '0');
                ++digits;
            }
            ++text;
        }
    }
    if (*text != '\0') return false;
    while (digits++ < 3) fraction *= 10;
    millivolts = whole * 1000 + fraction;
    return true;
}

// "12.780" from millivolts.
void formatVolts(char (&buffer)[16], std::int32_t millivolts) {
    const auto magnitude = std::labs(static_cast<long>(millivolts));
    std::snprintf(buffer, sizeof buffer, "%s%ld.%03ld", millivolts < 0 ? "-" : "", magnitude / 1000,
                  magnitude % 1000);
}

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
    nextWatchAt_ = now + watchPeriodMs;
    describeWatch();
    log_.writef(LogLevel::info, "input module: DI %u%u%u%u at start (1 = 12 V present)",
                static_cast<unsigned>(digital_[0]), static_cast<unsigned>(digital_[1]),
                static_cast<unsigned>(digital_[2]), static_cast<unsigned>(digital_[3]));
    return true;
}

void InputModuleTestService::run() {
    trackAnalog();
    watchDigital();
    const auto now = runtime_.millis();
    watch(now);
    report(now);
}

void InputModuleTestService::watch(std::uint32_t now) {
    if (watchMask_ == 0u || !due(now, nextWatchAt_)) return;
    nextWatchAt_ += watchPeriodMs;
    bool above = false;
    for (std::size_t channel = 0u; channel < channels; ++channel) {
        if ((watchMask_ & (1u << channel)) && module_.analogValid(channel) &&
            module_.analogMillivolts(channel) > watchThresholdMv_) {
            above = true;
        }
    }
    if (!above && !watchTriggered_) return;
    // "watch: AI1 12.780 V | AI2 0.006 V | AI3 0.006 V"
    char text[96]{};
    std::size_t used = 0u;
    for (std::size_t channel = 0u; channel < channels && used < sizeof text; ++channel) {
        if (!(watchMask_ & (1u << channel))) continue;
        char volts[16];
        formatVolts(volts, module_.analogValid(channel) ? module_.analogMillivolts(channel) : 0);
        used += static_cast<std::size_t>(std::snprintf(text + used, sizeof text - used, "%sAI%u %s V",
                                                       used ? " | " : "", static_cast<unsigned>(channel),
                                                       module_.analogValid(channel) ? volts : "--"));
    }
    char threshold[16];
    formatVolts(threshold, watchThresholdMv_);
    if (above) {
        log_.writef(LogLevel::info, "watch: %s", text);
    } else {
        log_.writef(LogLevel::info, "watch: all below %s V (%s)", threshold, text);
    }
    watchTriggered_ = above;
}

void InputModuleTestService::describeWatch() const {
    if (watchMask_ == 0u) {
        log_.write(LogLevel::info, "watch: off");
        return;
    }
    char threshold[16];
    formatVolts(threshold, watchThresholdMv_);
    log_.writef(LogLevel::info, "watch: AI%s%s%s%s above %s V, logged every second",
                (watchMask_ & 1u) ? "0" : "", (watchMask_ & 2u) ? "1" : "", (watchMask_ & 4u) ? "2" : "",
                (watchMask_ & 8u) ? "3" : "", threshold);
}

bool InputModuleTestService::registerCommands(slstm32::CommandConsole& console) {
    return console.add({"watch", "watch [off | <channels> [<volts>]]  log analog inputs above a threshold", &watchCommand, this});
}

void InputModuleTestService::watchCommand(void* context, std::size_t argc, const char* const* argv) {
    auto& self = *static_cast<InputModuleTestService*>(context);
    if (argc >= 2u && std::strcmp(argv[1], "off") == 0) {
        self.watchMask_ = 0u;
        self.watchTriggered_ = false;
    } else if (argc >= 2u) {
        std::uint8_t mask = 0u;
        for (const char* digit = argv[1]; *digit; ++digit) {
            if (*digit < '0' || *digit > '3') {
                self.log_.write(LogLevel::warning, "usage: watch [off | <channels 0..3> [<volts>]], e.g. watch 123 1.0");
                return;
            }
            mask = static_cast<std::uint8_t>(mask | (1u << (*digit - '0')));
        }
        std::int32_t threshold = self.watchThresholdMv_;
        if (argc >= 3u && !parseVolts(argv[2], threshold)) {
            self.log_.writef(LogLevel::warning, "watch: '%s' is not a voltage", argv[2]);
            return;
        }
        self.watchMask_ = mask;
        self.watchThresholdMv_ = threshold;
        self.watchTriggered_ = false;
        self.nextWatchAt_ = self.runtime_.millis();
    }
    self.describeWatch();
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
