#include "slstm32/console.hpp"
#include <cstring>

namespace slstm32 {

bool CommandConsole::add(const Command& command) {
    if (commandCount_ == maxCommands || !command.name || !command.handler) return false;
    commands_[commandCount_++] = command;
    return true;
}

bool CommandConsole::init() {
    if (!input_.read) return false;
    return !input_.configure || input_.configure(input_.context);
}

void CommandConsole::run() {
    std::uint8_t buffer[16];
    for (;;) {
        const auto count = input_.read(input_.context, buffer, sizeof buffer);
        if (count == 0u) return;
        for (std::size_t index = 0u; index < count; ++index) {
            const char character = static_cast<char>(buffer[index]);
            if (character == '\r' || character == '\n') {
                if (overflow_) {
                    log_.write(LogLevel::warning, "console: line too long, ignored");
                } else if (length_ != 0u) {
                    line_[length_] = '\0';
                    dispatch(line_.data());
                }
                length_ = 0u;
                overflow_ = false;
            } else if (length_ < lineCapacity) {
                line_[length_++] = character;
            } else {
                overflow_ = true;
            }
        }
    }
}

void CommandConsole::execute(const char* line) {
    // Own buffer: the serial input may be in the middle of a line.
    std::array<char, lineCapacity + 1u> copy{};
    std::size_t length = 0u;
    while (line && line[length] != '\0' && length < lineCapacity) {
        copy[length] = line[length];
        ++length;
    }
    if (length != 0u) dispatch(copy.data());
}

void CommandConsole::dispatch(char* line) {
    log_.writef(LogLevel::info, "> %s", line);
    std::array<const char*, maxArguments> argv{};
    std::size_t argc = 0u;
    char* cursor = line;
    while (*cursor != '\0' && argc < maxArguments) {
        while (*cursor == ' ') *cursor++ = '\0';
        if (*cursor == '\0') break;
        argv[argc++] = cursor;
        while (*cursor != '\0' && *cursor != ' ') ++cursor;
    }
    if (argc == 0u) return;
    if (std::strcmp(argv[0], "help") == 0) {
        help();
        return;
    }
    for (std::size_t index = 0u; index < commandCount_; ++index) {
        if (std::strcmp(argv[0], commands_[index].name) == 0) {
            commands_[index].handler(commands_[index].context, argc, argv.data());
            return;
        }
    }
    log_.writef(LogLevel::warning, "console: unknown command '%s', try help", argv[0]);
}

void CommandConsole::help() const {
    log_.write(LogLevel::info, "commands: help");
    for (std::size_t index = 0u; index < commandCount_; ++index) {
        log_.writef(LogLevel::info, "  %s", commands_[index].usage ? commands_[index].usage
                                                                 : commands_[index].name);
    }
}

} // namespace slstm32
