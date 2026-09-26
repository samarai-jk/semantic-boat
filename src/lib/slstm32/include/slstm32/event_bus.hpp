#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "slstm32/runtime.hpp"

namespace slstm32 {

using EventId = std::uint16_t;
using EventHandler = void (*)(EventId, const void*, std::uint8_t, void*);

class EventBus {
public:
    static constexpr std::size_t subscriptionCapacity = 24;
    static constexpr std::size_t eventCapacity = 32;
    static constexpr std::size_t payloadCapacity = 16;

    explicit EventBus(Runtime runtime) : runtime_(runtime) {}
    bool subscribe(EventId id, EventHandler handler, void* context = nullptr);
    bool unsubscribe(EventId id, EventHandler handler, void* context = nullptr);
    bool publish(EventId id, const void* payload = nullptr, std::uint8_t size = 0u);
    bool publishFromIsr(EventId id, const void* payload = nullptr, std::uint8_t size = 0u) {
        return publish(id, payload, size);
    }
    void process();
    std::uint32_t droppedEvents() const { return dropped_; }

    template <typename T>
    bool publish(EventId id, const T& value) {
        static_assert(sizeof(T) <= payloadCapacity, "event payload is too large");
        return publish(id, &value, static_cast<std::uint8_t>(sizeof(T)));
    }

    template <typename T>
    bool publishFromIsr(EventId id, const T& value) {
        static_assert(sizeof(T) <= payloadCapacity, "event payload is too large");
        return publishFromIsr(id, &value, static_cast<std::uint8_t>(sizeof(T)));
    }

private:
    struct Subscription {
        EventId id{};
        EventHandler handler{};
        void* context{};
        bool used{};
    };
    struct Event {
        EventId id{};
        std::uint8_t size{};
        std::array<std::uint8_t, payloadCapacity> payload{};
    };

    Runtime runtime_;
    std::array<Subscription, subscriptionCapacity> subscriptions_{};
    std::array<Event, eventCapacity> events_{};
    std::size_t head_{};
    std::size_t tail_{};
    std::uint32_t dropped_{};
};

} // namespace slstm32
