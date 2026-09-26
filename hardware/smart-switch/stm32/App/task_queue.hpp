#ifndef APP_TASK_QUEUE_HPP
#define APP_TASK_QUEUE_HPP

#include <cstdint>
#include "executor.hpp"

extern "C" {
#include "main.h" // for CMSIS SCB
}

namespace app {

class TaskQueue {
public:
    static constexpr uint32_t kCapacity = 32; // tune as needed
    TaskQueue() = default;

    // Enqueue a task from any context (ISR-safe). Returns false if full.
    bool enqueue(const Task& t);

    // Drain all tasks (call from low-priority context, e.g., PendSV or main loop)
    void processAll();

    // Trigger the async processor (PendSV on MCU)
    void triggerProcessing();

private:
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    // Ring buffer state
    volatile uint32_t head_ = 0; // next write
    volatile uint32_t tail_ = 0; // next read
    Task buffer_[kCapacity] = {};

    bool isFullUnsafe() const { return ((head_ + 1) % kCapacity) == tail_; }
    bool isEmptyUnsafe() const { return head_ == tail_; }
};

} // namespace app

#endif // APP_TASK_QUEUE_HPP
