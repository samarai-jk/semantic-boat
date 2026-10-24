#include "io_console.hpp"

namespace engine_mon {

bool IoConsole::registerCommands(slstm32::CommandConsole& console) {
    return console.add({"io", "io  host inputs and outputs (DI level, AI mV, DO duty)", &ioCommand, this});
}

void IoConsole::show() const {
    using slstm32::LogLevel;
    char levels[board::digitalInputCount + 1u]{};
    for (std::size_t index = 0u; index < board::digitalInputCount; ++index) {
        levels[index] = inputs_[index]->value() ? '1' : '0';
    }
    log_.writef(LogLevel::info, "io: DI0..7 %s (pin level; 0 = 12 V on an input module)", levels);
    for (std::size_t index = 0u; index < board::digitalInputCount; ++index) {
        if (inputs_[index]->changes() != 0u) {
            log_.writef(LogLevel::info, "io: DI%u %lu change(s)", static_cast<unsigned>(index),
                        static_cast<unsigned long>(inputs_[index]->changes()));
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
