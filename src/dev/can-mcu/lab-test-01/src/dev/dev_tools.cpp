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
                    context.redLed} {}

    bool init() override {
        bool ok = context_.console.add({"trap", "trap hang|fault  provoke a watchdog restart or a HardFault (dev)",
                                        &trapCommand, this});
        ok = context_.console.add({"i2c", "i2c [read <addr> <reg> [n] | write <addr> <reg> <value>]  raw bus access (dev)",
                                   &i2cCommand, this}) && ok;
        ok = context_.console.add({"lightning",
                                   "lightning [tune | antenna on|off | cap <0-15> | nf <0-7> | indoor | outdoor | disturbers on|off | poll <ms>] (dev)",
                                   &lightningCommand, this}) && ok;
        ok = hardwareTest_.init() && ok;
        ok = inputModuleTest_.init() && ok;
        ok = activity_.init() && ok;
        context_.log.write(LogLevel::warning,
                           "dev tools: hardware and module tests, background load, trap, i2c, lightning commands");
        return ok;
    }

    void run() override {
        hardwareTest_.run();
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

    // Lightning sensor bench tests: status, antenna resonance on the IRQ pin
    // (scope: 31.25 kHz = 500 kHz / 16), tuning, noise floor, front end gain.
    static void lightningCommand(void* context, std::size_t argc, const char* const* argv) {
        using Location = slstm32::drivers::As3935Driver::Location;
        auto& self = *static_cast<DevToolsService*>(context);
        auto& sensor = self.context_.lightning;
        const auto& log = self.context_.log;
        bool ok = true;
        if (argc == 3u && std::strcmp(argv[1], "antenna") == 0) {
            const bool on = std::strcmp(argv[2], "on") == 0;
            ok = sensor.showAntennaFrequency(on);
            if (ok && on) log.write(LogLevel::info, "lightning: IRQ pin shows antenna resonance / 16, expect 31.25 kHz +-3.5 %");
        } else if (argc == 3u && std::strcmp(argv[1], "cap") == 0) {
            ok = sensor.setTuningCapacitor(static_cast<std::uint8_t>(std::strtoul(argv[2], nullptr, 0)));
        } else if (argc == 3u && std::strcmp(argv[1], "nf") == 0) {
            ok = sensor.setNoiseFloor(static_cast<std::uint8_t>(std::strtoul(argv[2], nullptr, 0)));
        } else if (argc == 2u && std::strcmp(argv[1], "indoor") == 0) {
            ok = sensor.setLocation(Location::indoor);
        } else if (argc == 2u && std::strcmp(argv[1], "outdoor") == 0) {
            ok = sensor.setLocation(Location::outdoor);
        } else if (argc == 3u && std::strcmp(argv[1], "disturbers") == 0) {
            ok = sensor.setDisturberReporting(std::strcmp(argv[2], "on") == 0);
        } else if (argc == 2u && std::strcmp(argv[1], "tune") == 0) {
            ok = sensor.startAntennaTuning();
            if (ok) return; // the result is logged when done
        } else if (argc == 3u && std::strcmp(argv[1], "poll") == 0) {
            sensor.setPollPeriod(static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 0)));
        } else if (argc != 1u) {
            log.write(LogLevel::warning,
                      "usage: lightning [tune | antenna on|off | cap <0-15> | nf <0-7> | indoor | outdoor | "
                      "disturbers on|off | poll <ms>]");
            return;
        }
        if (!ok) log.write(LogLevel::warning, "lightning: failed (sensor absent or value out of range)");
        // Share of reads that found a disturber since the last status.
        const auto polls = sensor.polls() - self.lightningPolls_;
        const auto found = sensor.disturberCount() - self.lightningDisturbers_;
        self.lightningPolls_ = sensor.polls();
        self.lightningDisturbers_ = sensor.disturberCount();
        log.writef(LogLevel::info,
                   "lightning: %lu IRQ edges in total; since last status %lu reads, %lu with a disturber (%lu %%)",
                   static_cast<unsigned long>(sensor.interrupts()), static_cast<unsigned long>(polls),
                   static_cast<unsigned long>(found), static_cast<unsigned long>(polls ? found * 100u / polls : 0u));
        log.writef(LogLevel::info,
                   "lightning: %s, %s, tuning %u pF, noise floor %u, disturbers %s; %lu lightning (last %u km, energy %lu), "
                   "%lu disturbers, %lu noise high",
                   sensor.present() ? (sensor.calibrated() ? "present" : "present, NOT calibrated") : "absent",
                   sensor.location() == Location::outdoor ? "outdoor" : "indoor",
                   static_cast<unsigned>(sensor.tuningCapacitor()) * 8u, static_cast<unsigned>(sensor.noiseFloor()),
                   sensor.disturberReporting() ? "reported" : "masked",
                   static_cast<unsigned long>(sensor.lightningCount()), static_cast<unsigned>(sensor.lastDistanceKm()),
                   static_cast<unsigned long>(sensor.lastEnergy()), static_cast<unsigned long>(sensor.disturberCount()),
                   static_cast<unsigned long>(sensor.noiseHighCount()));
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
    std::uint32_t lightningPolls_{};
    std::uint32_t lightningDisturbers_{};
    HardwareTestService hardwareTest_;
    InputModuleTestService inputModuleTest_;
    ActivityService activity_;
};

} // namespace

slstm32::Service& devTools(const DevToolsContext& context) {
    static DevToolsService service{context};
    return service;
}

} // namespace lab_test_01
