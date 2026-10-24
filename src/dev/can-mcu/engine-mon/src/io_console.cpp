#include "io_console.hpp"
#include <cstdio>

namespace engine_mon {

namespace {

// "12.34 V" from mV, without floating point.
const char* volts(char (&out)[16], std::int32_t millivolts) {
    const auto magnitude = millivolts < 0 ? -millivolts : millivolts;
    std::snprintf(out, sizeof out, "%s%ld.%02ld V", millivolts < 0 ? "-" : "", static_cast<long>(magnitude / 1000),
                  static_cast<long>(magnitude % 1000 / 10));
    return out;
}

} // namespace

bool IoConsole::registerCommands(slstm32::CommandConsole& console) {
    return console.add({"io", "io  host inputs and outputs (AD1/AD2 inputs, MCU analog, outputs)", &ioCommand, this});
}

void IoConsole::show() const {
    using slstm32::LogLevel;
    constexpr std::size_t perModule = semantic_modules::AdInputIso12v::channels;
    for (std::size_t index = 0u; index < board::digitalInputCount; ++index) {
        const auto& module = *modules_[index / perModule];
        const auto channel = index % perModule;
        log_.writef(LogLevel::info, "io: %-23s %s, %lu change(s)", board::digitalInputNames[index],
                    module.digitalActive(channel) ? "12 V" : "off",
                    static_cast<unsigned long>(module.digitalChanges(channel)));
    }
    for (std::size_t index = 0u; index < board::moduleAnalogCount; ++index) {
        const auto& module = *modules_[index / perModule];
        const auto channel = index % perModule;
        char text[16];
        if (module.analogValid(channel)) {
            log_.writef(LogLevel::info, "io: %-23s %s", board::moduleAnalogNames[index],
                        volts(text, module.analogMillivolts(channel)));
        } else {
            log_.writef(LogLevel::info, "io: %-23s - (module 0x%02X not answering)", board::moduleAnalogNames[index],
                        static_cast<unsigned>(module.adcAddress()));
        }
    }
    for (std::size_t index = 0u; index < board::analogInputCount; ++index) {
        const auto input = analogInputs_[index];
        if (mcu_.inputValid(input)) {
            log_.writef(LogLevel::info, "io: AI%u %ld mV", static_cast<unsigned>(index),
                        static_cast<long>(mcu_.inputMillivolts(input)));
        } else {
            log_.writef(LogLevel::info, "io: AI%u -", static_cast<unsigned>(index));
        }
    }
    for (std::size_t index = 0u; index < board::outputCount; ++index) {
        log_.writef(LogLevel::info, "io: DO%u %u %%", static_cast<unsigned>(index),
                    static_cast<unsigned>(outputs_[index]->duty() * 100.0f + 0.5f));
    }
}

void IoConsole::ioCommand(void* context, std::size_t, const char* const*) {
    static_cast<IoConsole*>(context)->show();
}

} // namespace engine_mon
