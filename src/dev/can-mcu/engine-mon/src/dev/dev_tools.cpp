// DEVELOPMENT AND TEST CODE: compiled only with SB_DEV_TOOLS (AGENTS.md,
// "Firmware code categories and build profiles"). Never reference this from
// product code.
//
// Bring-up of the engine-mon host: "do <n> <percent>" switches output n by
// hand (0 off, 100 on, in between PWM at 1 kHz). Outputs drive real loads
// (relays, horn), so this never belongs in a product build.
#include "dev_tools.hpp"
#include <cstdlib>

namespace engine_mon {
namespace {

class DevToolsService final : public slstm32::Service {
public:
    explicit DevToolsService(const DevToolsContext& context) : context_(context) {}

    bool init() override {
        return context_.console.add({"do", "do <n> <0..100>  DEV: set output n by hand (percent)", &doCommand, this});
    }
    void run() override {
        // TEMPORARY (output test with a meter or scope, 2026-10-24): DO0 1 Hz,
        // DO1 2 Hz, DO2 5 Hz, each fully on for half the period. Remove after
        // the test. Keep the arming switch off: these are the preheater,
        // starter, and stop solenoid relays.
        constexpr std::uint32_t periodsMs[]{1000u, 500u, 200u};
        if (!outputTestLogged_) {
            outputTestLogged_ = true;
            context_.log.write(slstm32::LogLevel::warning,
                               "DEV: output test running: DO0 1 Hz, DO1 2 Hz, DO2 5 Hz (arming switch off!)");
        }
        const auto now = context_.runtime.millis();
        for (std::size_t index = 0u; index < context_.outputCount && index < 3u; ++index) {
            const bool on = now % periodsMs[index] < periodsMs[index] / 2u;
            if (on != outputOn_[index]) {
                outputOn_[index] = on;
                context_.outputs[index]->setDuty(on ? 1.0f : 0.0f);
            }
        }
    }

private:
    static void doCommand(void* self, std::size_t argc, const char* const* argv) {
        auto& tools = *static_cast<DevToolsService*>(self);
        const auto& context = tools.context_;
        char* end = nullptr;
        const auto index = argc == 3u ? std::strtoul(argv[1], &end, 10) : context.outputCount;
        const bool indexValid = argc == 3u && *end == '\0' && index < context.outputCount;
        const auto percent = indexValid ? std::strtoul(argv[2], &end, 10) : 101u;
        if (!indexValid || *end != '\0' || percent > 100u) {
            context.log.writef(slstm32::LogLevel::error, "usage: do <0..%u> <0..100>",
                               static_cast<unsigned>(context.outputCount - 1u));
            return;
        }
        context.outputs[index]->setDuty(static_cast<float>(percent) / 100.0f);
        context.log.writef(slstm32::LogLevel::warning, "DEV: DO%lu set to %lu %%", index, percent);
    }

    const DevToolsContext& context_;
    bool outputTestLogged_{};
    bool outputOn_[3]{};
};

} // namespace

slstm32::Service& devTools(const DevToolsContext& context) {
    static DevToolsService tools{context};
    return tools;
}

} // namespace engine_mon
