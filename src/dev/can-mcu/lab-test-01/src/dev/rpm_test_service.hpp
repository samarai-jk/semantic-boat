// DEVELOPMENT AND TEST CODE: compiled only with SB_DEV_TOOLS (AGENTS.md,
// "Firmware code categories and build profiles"). Never reference this from
// product code.
#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/console.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace lab_test_01 {

// TEMPORARY frequency test of the 12v-ad-input-iso digital inputs (engine
// rpm): TIM2 PWM on PA0 drives an isolated MOSFET switch that switches 12 V
// onto D1; OUT_D_1 arrives on PA5, where both edges are timestamped with the
// cycle counter. Once per second: generated and measured frequency, missing
// or extra pulses, duty cycle at the MCU, period spread. Console "rpm".
// Remove after the test.
class RpmTestService final : public slstm32::Service {
public:
    RpmTestService(slstm32::Runtime runtime, slstm32::Logger log, slstm32::CommandConsole& console)
        : runtime_(runtime), log_(log), console_(console) {}

    bool init() override;
    void run() override;
    // EXTI callback (both edges of PA5).
    static void onEdge(std::uint16_t pin);

private:
    static void command(void* context, std::size_t argc, const char* const* argv);
    void generate(std::uint32_t milliHz, std::uint8_t duty);
    void report(std::uint32_t now);

    slstm32::Runtime runtime_;
    slstm32::Logger log_;
    slstm32::CommandConsole& console_;
    std::uint32_t milliHz_{};
    std::uint8_t duty_{50u};
    std::uint32_t reportAt_{};
    std::uint32_t windowStart_{};
    bool sweeping_{};
    std::size_t sweepStep_{};
    std::uint32_t sweepNextAt_{};
};

} // namespace lab_test_01
