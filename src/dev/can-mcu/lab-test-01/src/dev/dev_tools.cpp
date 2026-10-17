// DEVELOPMENT AND TEST CODE: compiled only with SB_DEV_TOOLS (AGENTS.md,
// "Firmware code categories and build profiles"). Never reference anything in
// src/dev/ from product code; extend dev_tools.hpp instead.
#include "dev_tools.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "activity_service.hpp"
#include "hardware_test_service.hpp"
#include "input_module_test_service.hpp"
#include "speaker_test_service.hpp"
#include "events.hpp"

namespace lab_test_01 {
namespace {

using slstm32::LogLevel;

constexpr std::uint32_t flushBeforeTrapMs = 200u;

class DevToolsService final : public slstm32::Service {
public:
    explicit DevToolsService(const DevToolsContext& context)
        : context_(context),
          hardwareTest_{context.runtime, context.events, context.log, context.buttons, context.buttonCount,
                        context.greenLed, context.redLed, context.buzzer},
          inputModuleTest_{context.runtime, context.log, context.inputModule},
          activity_{context.runtime, context.n2kNode, context.inputModule, context.health, context.greenLed,
                    context.redLed},
          speakerTest_{context.runtime, context.events, buttonPressed, context.log, context.console, context.buzzer} {}

    bool init() override {
        bool ok = context_.console.add({"trap", "trap hang|fault  provoke a watchdog restart or a HardFault (dev)",
                                        &trapCommand, this});
        ok = context_.console.add({"i2c", "i2c [read <addr> <reg> [n] | write <addr> <reg> <value>]  raw bus access (dev)",
                                   &i2cCommand, this}) && ok;
        // TEMPORARY: the speaker test uses the buttons instead of the hardware test.
        ok = speakerTest_.init() && ok;
        ok = inputModuleTest_.init() && ok;
        ok = activity_.init() && ok;
        context_.log.write(LogLevel::warning,
                           "dev tools: hardware and module tests, background load, trap and i2c commands");
        return ok;
    }

    void run() override {
        speakerTest_.run();
        inputModuleTest_.run();
        activity_.run();
    }

private:
    // Deliberate failures to verify the supervision traps before long runs.
    static void trapCommand(void* context, std::size_t argc, const char* const* argv) {
        auto& self = *static_cast<DevToolsService*>(context);
        if (argc == 2u && std::strcmp(argv[1], "hang") == 0) {
            self.context_.log.write(LogLevel::warning, "trap: hanging, expect a watchdog restart");
            self.context_.serialLog.flush(flushBeforeTrapMs);
            for (;;) {
            }
        }
        if (argc == 2u && std::strcmp(argv[1], "fault") == 0) {
            self.context_.log.write(LogLevel::warning, "trap: invalid memory access, expect a HardFault");
            self.context_.serialLog.flush(flushBeforeTrapMs);
            (void)*reinterpret_cast<volatile std::uint32_t*>(0xFFFFFFF0u);
        }
        self.context_.log.write(LogLevel::warning, "usage: trap hang|fault");
    }

    // Raw register access for exploring sensors.
    static void i2cCommand(void* context, std::size_t argc, const char* const* argv) {
        auto& self = *static_cast<DevToolsService*>(context);
        const auto& log = self.context_.log;
        auto& bus = self.context_.i2c;
        const auto number = [](const char* text, unsigned long maximum, unsigned long& value) {
            char* end = nullptr;
            value = std::strtoul(text, &end, 0);
            return end != text && *end == '\0' && value <= maximum;
        };
        if (argc == 1u) {
            self.context_.i2cScan.logPresent();
            return;
        }
        unsigned long address{};
        unsigned long reg{};
        if (argc >= 4u && std::strcmp(argv[1], "read") == 0 && number(argv[2], 0x7Fu, address) &&
            number(argv[3], 0xFFu, reg)) {
            unsigned long count = 2u;
            if (argc >= 5u && (!number(argv[4], 16u, count) || count == 0u)) {
                log.write(LogLevel::warning, "i2c read: count must be 1..16");
                return;
            }
            std::uint8_t data[16]{};
            if (!bus.readRegister(static_cast<std::uint8_t>(address), static_cast<std::uint8_t>(reg), data, count)) {
                log.writef(LogLevel::warning, "i2c 0x%02lX reg 0x%02lX: read failed", address, reg);
                return;
            }
            char text[3u * 16u + 1u]{};
            for (std::size_t index = 0u; index < count; ++index) {
                std::snprintf(text + 3u * index, 4u, " %02X", static_cast<unsigned>(data[index]));
            }
            log.writef(LogLevel::info, "i2c 0x%02lX reg 0x%02lX:%s", address, reg, text);
            return;
        }
        unsigned long value{};
        if (argc == 5u && std::strcmp(argv[1], "write") == 0 && number(argv[2], 0x7Fu, address) &&
            number(argv[3], 0xFFu, reg) && number(argv[4], 0xFFu, value)) {
            const bool ok = bus.writeRegister(static_cast<std::uint8_t>(address), static_cast<std::uint8_t>(reg),
                                              static_cast<std::uint8_t>(value));
            log.writef(ok ? LogLevel::info : LogLevel::warning, "i2c 0x%02lX reg 0x%02lX <- 0x%02lX: %s", address,
                       reg, value, ok ? "ok" : "failed");
            return;
        }
        log.write(LogLevel::warning, "usage: i2c [read <addr> <reg> [n] | write <addr> <reg> <value>]");
    }

    DevToolsContext context_;
    HardwareTestService hardwareTest_;
    InputModuleTestService inputModuleTest_;
    ActivityService activity_;
    SpeakerTestService speakerTest_;
};

} // namespace

slstm32::Service& devTools(const DevToolsContext& context) {
    static DevToolsService service{context};
    return service;
}

} // namespace lab_test_01
