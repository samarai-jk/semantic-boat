#include "executor_bare.hpp"

extern "C" {
#include "main.h"
}

namespace app {

static inline uint32_t now_ms() { return HAL_GetTick(); }

Executor& Executor::instance() { return BareExecutor::inst(); }

BareExecutor& BareExecutor::inst() {
    static BareExecutor e;
    return e;
}

bool BareExecutor::post(const Task& t) { return post(t, false); }
bool BareExecutor::postFromISR(const Task& t) { return postFromISR(t, false); }
bool BareExecutor::post(const Task& t, bool high) {
    bool ok = high ? highQ_.enqueue(t) : normQ_.enqueue(t);
    if (ok) trigger();
    return ok;
}
bool BareExecutor::postFromISR(const Task& t, bool high) {
    bool ok = high ? highQ_.enqueue(t) : normQ_.enqueue(t);
    if (ok) trigger();
    return ok;
}

TimerHandle BareExecutor::callLater(uint32_t delay_ms, void (*func)(void*), void* ctx) {
    uint32_t due = now_ms() + delay_ms;
    for (int i = 0; i < kMaxTimers; ++i) {
        if (!timers_[i].active) {
            timers_[i] = {due, 0u, func, ctx, true};
            trigger();
            return i;
        }
    }
    return -1;
}

TimerHandle BareExecutor::callEvery(uint32_t period_ms, void (*func)(void*), void* ctx) {
    uint32_t due = now_ms() + period_ms;
    for (int i = 0; i < kMaxTimers; ++i) {
        if (!timers_[i].active) {
            timers_[i] = {due, period_ms, func, ctx, true};
            trigger();
            return i;
        }
    }
    return -1;
}

bool BareExecutor::cancel(TimerHandle h) {
    if (h < 0 || h >= kMaxTimers) return false;
    timers_[h].active = false;
    return true;
}

void BareExecutor::trigger() {
    // Pend the PendSV to process timers and queues soon
    highQ_.triggerProcessing();
}

void BareExecutor::process() {
    // Fire due timers (non-ISR context)
    uint32_t now = now_ms();
    for (int i = 0; i < kMaxTimers; ++i) {
        auto& t = timers_[i];
        if (!t.active || !t.func) continue;
        // Handle tick wrap-around safely
        if ((int32_t)(now - t.next_ms) >= 0) {
            // Schedule callback by posting to task queue to serialize execution
            Task task{t.func, t.ctx};
            highQ_.enqueue(task);
            if (t.period_ms) {
                // periodic: move next due by period; keep up even if delayed
                t.next_ms += t.period_ms;
            } else {
                // one-shot
                t.active = false;
            }
        }
    }
    // Drain high priority first, then normal
    highQ_.processAll();
    normQ_.processAll();
}

} // namespace app
