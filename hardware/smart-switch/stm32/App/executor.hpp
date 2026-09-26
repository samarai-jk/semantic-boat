#ifndef APP_EXECUTOR_HPP
#define APP_EXECUTOR_HPP

#ifndef __cplusplus
#error "executor.hpp requires C++ (include from a .cpp file)"
#endif

#include <cstdint>

namespace app {

struct Task {
    void (*func)(void*);
    void* ctx;
};

using TimerHandle = int32_t; // -1 invalid

class Executor {
public:
    static Executor& instance();

    // Post a task to be executed asynchronously (not in ISR)
    virtual bool post(const Task& t) = 0;
    // ISR-safe post
    virtual bool postFromISR(const Task& t) = 0;
    // Priority posting (true for high priority)
    virtual bool post(const Task& t, bool high) = 0;
    virtual bool postFromISR(const Task& t, bool high) = 0;

    // Timers (callbacks run in executor context, not ISR)
    virtual TimerHandle callLater(uint32_t delay_ms, void (*func)(void*), void* ctx) = 0;
    virtual TimerHandle callEvery(uint32_t period_ms, void (*func)(void*), void* ctx) = 0;
    virtual bool cancel(TimerHandle h) = 0;

    // Internal hooks for backend
    virtual void trigger() = 0;   // cause processing to run soon
    virtual void process() = 0;   // drain ready timers and tasks

protected:
    Executor() = default;
    virtual ~Executor() = default;
};

} // namespace app

#endif // APP_EXECUTOR_HPP
