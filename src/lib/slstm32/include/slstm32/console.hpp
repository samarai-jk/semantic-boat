#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "slstm32/drivers/hardware.hpp"
#include "slstm32/log.hpp"
#include "slstm32/service.hpp"

namespace slstm32 {

// Line-based command console on a serial input, for bring-up and service
// tasks. Received lines (terminated by CR or LF) are echoed to the log as
// "> line", split at spaces into at most eight arguments, and dispatched to
// the registered command whose name matches the first argument. Handlers
// write their answers to the log. "help" lists all commands.
//
// Handlers run inside run() and must not block. Lines longer than
// lineCapacity are discarded.
class CommandConsole final : public Service {
public:
    static constexpr std::size_t maxCommands = 16u;
    static constexpr std::size_t lineCapacity = 80u;
    static constexpr std::size_t maxArguments = 8u;

    using Handler = void (*)(void* context, std::size_t argc, const char* const* argv);
    struct Command {
        const char* name{};
        const char* usage{}; // shown by "help", e.g. "cal <addr> <ch> <volts|zero|reset>"
        Handler handler{};
        void* context{};
    };

    CommandConsole(drivers::SerialInputHardware input, Logger log) : input_(input), log_(log) {}

    // Register before or after init(). Returns false when full.
    bool add(const Command& command);
    bool init() override;
    void run() override;
    const Logger& log() const { return log_; }
    // Runs a command line from another source (for example a remote console
    // over CAN), as if it had been typed. Longer lines are truncated.
    void execute(const char* line);

private:
    void dispatch(char* line);
    void help() const;

    drivers::SerialInputHardware input_;
    Logger log_;
    std::array<Command, maxCommands> commands_{};
    std::size_t commandCount_{};
    std::array<char, lineCapacity + 1u> line_{};
    std::size_t length_{};
    bool overflow_{};
};

} // namespace slstm32
