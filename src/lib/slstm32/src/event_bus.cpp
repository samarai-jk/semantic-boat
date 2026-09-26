#include "slstm32/event_bus.hpp"

namespace slstm32 {

bool EventBus::subscribe(EventId id, EventHandler handler, void* context) {
    if (!handler) return false;
    for (const auto& sub : subscriptions_) {
        if (sub.used && sub.id == id && sub.handler == handler && sub.context == context) return true;
    }
    for (auto& sub : subscriptions_) {
        if (!sub.used) {
            sub = Subscription{id, handler, context, true};
            return true;
        }
    }
    return false;
}

bool EventBus::unsubscribe(EventId id, EventHandler handler, void* context) {
    for (auto& sub : subscriptions_) {
        if (sub.used && sub.id == id && sub.handler == handler && sub.context == context) {
            sub.used = false;
            return true;
        }
    }
    return false;
}

bool EventBus::publish(EventId id, const void* payload, std::uint8_t size) {
    if (size > payloadCapacity || (size != 0u && payload == nullptr)) return false;
    CriticalSection lock(runtime_);
    const auto next = (head_ + 1u) % eventCapacity;
    if (next == tail_) {
        ++dropped_;
        return false;
    }
    auto& event = events_[head_];
    event.id = id;
    event.size = size;
    if (size != 0u) std::memcpy(event.payload.data(), payload, size);
    head_ = next;
    return true;
}

void EventBus::process() {
    for (;;) {
        Event event{};
        {
            CriticalSection lock(runtime_);
            if (tail_ == head_) break;
            event = events_[tail_];
            tail_ = (tail_ + 1u) % eventCapacity;
        }
        for (const auto& sub : subscriptions_) {
            if (sub.used && sub.id == event.id) {
                sub.handler(event.id, event.size ? event.payload.data() : nullptr,
                            event.size, sub.context);
            }
        }
    }
}

} // namespace slstm32
