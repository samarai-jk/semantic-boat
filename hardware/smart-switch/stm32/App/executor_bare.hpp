#ifndef APP_EXECUTOR_BARE_HPP
#define APP_EXECUTOR_BARE_HPP

#include "executor.hpp"
#include "task_queue.hpp"

namespace app {

class BareExecutor : public Executor {
public:
    static BareExecutor& inst();

    bool post(const Task& t) override;
    bool postFromISR(const Task& t) override;
    bool post(const Task& t, bool high) override;
    bool postFromISR(const Task& t, bool high) override;

    TimerHandle callLater(uint32_t delay_ms, void (*func)(void*), void* ctx) override;
    TimerHandle callEvery(uint32_t period_ms, void (*func)(void*), void* ctx) override;
    bool cancel(TimerHandle h) override;

    void trigger() override;
    void process() override;

private:
    BareExecutor() = default;

    struct TimerRec {
        uint32_t next_ms;
        uint32_t period_ms; // 0 for one-shot
        void (*func)(void*);
        void* ctx;
        bool active;
    };
    static constexpr int kMaxTimers = 16;
    TimerRec timers_[kMaxTimers]{};

    // Two queues: high and normal
    TaskQueue highQ_;
    TaskQueue normQ_;
};

} // namespace app

#endif // APP_EXECUTOR_BARE_HPP
