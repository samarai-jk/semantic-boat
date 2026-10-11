#include "slstm32/drivers/button.hpp"

namespace slstm32::drivers {

bool ButtonDriver::init() {
    if (!buttons_ || count_ == 0u || count_ > capacity || !runtime_.millis) return false;
    for (std::size_t index = 0; index < count_; ++index) {
        const auto& hardware = buttons_[index].hardware;
        if (!hardware.read) return false;
        if (hardware.configure && !hardware.configure(hardware.context)) return false;
    }
    return true;
}

bool ButtonDriver::pressed(std::uint8_t id) const {
    for (std::size_t index = 0; index < count_; ++index) {
        if (buttons_[index].id != id) continue;
        const auto& hardware = buttons_[index].hardware;
        const bool physical = hardware.read(hardware.context);
        return hardware.activeHigh ? physical : !physical;
    }
    return false;
}

void ButtonDriver::resetDebounce() {
    CriticalSection lock(runtime_);
    seenMask_ = 0u;
}

bool ButtonDriver::onInterrupt(std::uint32_t token) {
    for (std::size_t index = 0; index < count_; ++index) {
        if (buttons_[index].interruptToken != token) continue;
        const auto now = runtime_.millis();
        const auto mask = static_cast<std::uint16_t>(1u << index);
        if ((seenMask_ & mask) != 0u &&
            static_cast<std::uint32_t>(now - lastEdgeMs_[index]) < debounceMs_) return false;
        seenMask_ = static_cast<std::uint16_t>(seenMask_ | mask);
        lastEdgeMs_[index] = now;
        bool isPressed{};
        switch (buttons_[index].interrupt) {
        case ButtonInterrupt::pressed: isPressed = true; break;
        case ButtonInterrupt::released: isPressed = false; break;
        case ButtonInterrupt::sampleInput: isPressed = pressed(buttons_[index].id); break;
        }
        const ButtonEvent event{buttons_[index].id, isPressed};
        return events_.publishFromIsr(eventId_, event);
    }
    return false;
}

} // namespace slstm32::drivers
