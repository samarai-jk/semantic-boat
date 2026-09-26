#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "slstm32/runtime.hpp"

namespace slstm32 {

struct Task { void (*function)(void*){}; void* context{}; };
using TimerHandle = std::int8_t;
inline constexpr TimerHandle invalidTimer = -1;

class BareExecutor {
public:
    static constexpr std::size_t taskCapacity = 32;
    static constexpr std::size_t timerCapacity = 16;

    explicit BareExecutor(Runtime runtime) : runtime_(runtime) {}
    bool post(Task task);
    bool postFromIsr(Task task) { return post(task); }
    TimerHandle callLater(std::uint32_t delayMs, Task task);
    TimerHandle callEvery(std::uint32_t periodMs, Task task);
    bool cancel(TimerHandle handle);
    void process();

private:
    struct Timer {
        std::uint32_t deadline{};
        std::uint32_t period{};
        Task task{};
        bool active{};
    };

    Runtime runtime_;
    std::array<Task, taskCapacity> tasks_{};
    std::array<Timer, timerCapacity> timers_{};
    std::size_t head_{};
    std::size_t tail_{};
};

} // namespace slstm32
