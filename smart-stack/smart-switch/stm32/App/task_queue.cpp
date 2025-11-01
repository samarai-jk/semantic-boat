#include "task_queue.hpp"

namespace app {

bool TaskQueue::enqueue(const Task& t) {
    // Disable IRQs briefly to update head/tail safely (works in ISR and thread)
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    bool ok = false;
    if (!isFullUnsafe()) {
        buffer_[head_] = t;
        head_ = (head_ + 1) % kCapacity;
        ok = true;
    }
    if (!primask) __enable_irq();
    if (ok) {
        triggerProcessing();
    }
    return ok;
}

void TaskQueue::processAll() {
    for (;;) {
        Task task;
        {
            uint32_t primask = __get_PRIMASK();
            __disable_irq();
            if (isEmptyUnsafe()) {
                if (!primask) __enable_irq();
                break;
            }
            task = buffer_[tail_];
            tail_ = (tail_ + 1) % kCapacity;
            if (!primask) __enable_irq();
        }
        if (task.func) {
            task.func(task.ctx);
        }
    }
}

void TaskQueue::triggerProcessing() {
    // On Cortex-M, PendSV is ideal for lowest-priority deferred work
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
}

} // namespace app
