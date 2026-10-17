#pragma once

#include <array>
#include <cstdint>
#include "semantic_modules/ad_input_iso_12v.hpp"
#include "slstm32/console.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace lab_test_01 {

// Bring-up of a 12v-ad-input-iso module: logs when the ADC appears or
// disappears, every digital input change as it happens, and once per second
// the four terminal voltages with their peak-to-peak noise, the digital
// states, and the analog sample rate.
//
// Voltage watch: while a watched channel is above the threshold, its
// voltages are logged every second, and once more when all are below it
// again. Starts with A1..A3 and 1 V. Console: "watch [off | <channels>
// [<volts>]]", for example "watch 123 1.0" or "watch 0123 0.5".
class InputModuleTestService final : public slstm32::Service {
public:
    InputModuleTestService(slstm32::Runtime runtime, slstm32::Logger log,
                           semantic_modules::AdInputIso12v& module)
        : runtime_(runtime), log_(log), module_(module) {}

    bool init() override;
    void run() override;
    bool registerCommands(slstm32::CommandConsole& console);

private:
    static void watchCommand(void* context, std::size_t argc, const char* const* argv);
    void watch(std::uint32_t now);
    void describeWatch() const;
    static constexpr std::size_t channels = semantic_modules::AdInputIso12v::channels;

    void trackAnalog();
    void watchDigital();
    void report(std::uint32_t now);
    void resetWindow();

    slstm32::Runtime runtime_;
    slstm32::Logger log_;
    semantic_modules::AdInputIso12v& module_;
    std::uint32_t nextReportAt_{};
    std::uint32_t nextWatchAt_{};
    std::uint8_t watchMask_{0b1110u};
    std::int32_t watchThresholdMv_{1000};
    bool watchTriggered_{};
    std::uint32_t windowStartedAt_{};
    bool adcPresent_{};
    std::array<bool, channels> digital_{};
    std::array<std::uint32_t, channels> lastSamples_{};
    std::array<std::uint32_t, channels> windowSamples_{};
    std::array<std::int32_t, channels> minimumMv_{};
    std::array<std::int32_t, channels> maximumMv_{};
};

} // namespace lab_test_01
