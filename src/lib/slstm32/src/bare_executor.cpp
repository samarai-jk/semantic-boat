#include "slstm32/executor.hpp"

namespace slstm32 {

bool BareExecutor::post(Task task) {
    if (!task.function) return false;
    CriticalSection lock(runtime_);
    const auto next = (head_ + 1u) % taskCapacity;
    if (next == tail_) return false;
    tasks_[head_] = task;
    head_ = next;
    return true;
}

TimerHandle BareExecutor::callLater(std::uint32_t delayMs, Task task) {
    if (!runtime_.millis || !task.function) return invalidTimer;
    for (std::size_t i = 0; i < timers_.size(); ++i) {
        if (!timers_[i].active) {
            timers_[i] = Timer{runtime_.millis() + delayMs, 0u, task, true};
            return static_cast<TimerHandle>(i);
        }
    }
    return invalidTimer;
}

TimerHandle BareExecutor::callEvery(std::uint32_t periodMs, Task task) {
    if (periodMs == 0u || !runtime_.millis || !task.function) return invalidTimer;
    for (std::size_t i = 0; i < timers_.size(); ++i) {
        if (!timers_[i].active) {
            timers_[i] = Timer{runtime_.millis() + periodMs, periodMs, task, true};
            return static_cast<TimerHandle>(i);
        }
    }
    return invalidTimer;
}

bool BareExecutor::cancel(TimerHandle handle) {
    if (handle < 0 || static_cast<std::size_t>(handle) >= timers_.size()) return false;
    timers_[static_cast<std::size_t>(handle)].active = false;
    return true;
}

void BareExecutor::process() {
    if (runtime_.millis) {
        const auto now = runtime_.millis();
        for (auto& timer : timers_) {
            if (!timer.active || static_cast<std::int32_t>(now - timer.deadline) < 0) continue;
            (void)post(timer.task);
            if (timer.period == 0u) timer.active = false;
            else do { timer.deadline += timer.period; }
                 while (static_cast<std::int32_t>(now - timer.deadline) >= 0);
        }
    }

    for (;;) {
        Task task{};
        {
            CriticalSection lock(runtime_);
            if (tail_ == head_) break;
            task = tasks_[tail_];
            tail_ = (tail_ + 1u) % taskCapacity;
        }
        task.function(task.context);
    }
}

} // namespace slstm32
