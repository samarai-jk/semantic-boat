#ifndef APP_EVENT_BUS_HPP
#define APP_EVENT_BUS_HPP

#include <cstdint>
#include <cstddef>

namespace app {

using EventId = uint16_t;

// Event handler signature: called in executor context (not ISR)
using EventHandler = void (*)(EventId evt, const void* payload, uint16_t size, void* ctx);

class EventBus {
public:
    static EventBus& instance();

    bool subscribe(EventId evt, EventHandler h, void* ctx);
    bool unsubscribe(EventId evt, EventHandler h, void* ctx);

    bool publish(EventId evt, const void* payload = nullptr, uint16_t size = 0);
    bool publishFromISR(EventId evt, const void* payload = nullptr, uint16_t size = 0);

private:
    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    struct Sub {
        EventId evt;
        EventHandler h;
        void* ctx;
        bool used;
    };
    static constexpr int kMaxSubs = 16;
    Sub subs_[kMaxSubs]{};
};

} // namespace app

#endif // APP_EVENT_BUS_HPP
