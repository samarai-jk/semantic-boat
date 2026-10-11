#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/hardware.hpp"
#include "slstm32/event_bus.hpp"
#include "slstm32/runtime.hpp"

namespace slstm32::drivers {

struct ButtonEvent {
    std::uint8_t id{};
    bool pressed{};
};

enum class ButtonInterrupt : std::uint8_t {
    sampleInput,
    pressed,
    released,
};

struct ButtonConfig {
    std::uint8_t id{};
    std::uint32_t interruptToken{};
    DigitalInputHardware hardware{};
    ButtonInterrupt interrupt{ButtonInterrupt::sampleInput};
};

class ButtonDriver final : public Driver {
public:
    static constexpr std::size_t capacity = 16u;

    ButtonDriver(Runtime runtime, EventBus& events, EventId eventId,
                 const ButtonConfig* buttons, std::size_t count,
                 std::uint32_t debounceMs = 25u)
        : runtime_(runtime), events_(events), eventId_(eventId), buttons_(buttons),
          count_(count), debounceMs_(debounceMs) {}

    bool init() override;
    void run() override {}
    bool onInterrupt(std::uint32_t token);
    bool pressed(std::uint8_t id) const;
    void resetDebounce();

private:
    Runtime runtime_;
    EventBus& events_;
    EventId eventId_{};
    const ButtonConfig* buttons_{};
    std::size_t count_{};
    std::uint32_t debounceMs_{};
    std::array<std::uint32_t, capacity> lastEdgeMs_{};
    std::uint16_t seenMask_{};
};

} // namespace slstm32::drivers
