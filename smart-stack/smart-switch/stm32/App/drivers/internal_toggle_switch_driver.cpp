#include "drivers/internal_toggle_switch_driver.hpp"
#include "events.hpp"
#include "event_bus.hpp"
#include "executor.hpp"

namespace app {

void InternalToggleSwitchDriver::init() {
    // Ensure LED output is low initially
    HAL_GPIO_WritePin(kLedPort, kLedPin, GPIO_PIN_RESET);
}

void InternalToggleSwitchDriver::run() {
    // Apply LED state (idempotent)
    HAL_GPIO_WritePin(kLedPort, kLedPin, output_on_ ? GPIO_PIN_SET : GPIO_PIN_RESET);

    if (pending_) {
        uint32_t now = HAL_GetTick();
        if ((now - last_irq_tick_) >= debounce_ms_) {
            pending_ = false;
            if (HAL_GPIO_ReadPin(kBtnPort, kBtnPin) == GPIO_PIN_RESET) {
                EventBus::instance().publish(EVT_TOGGLE_BUTTON_PRESSED);
            }
        }
    }
}

void InternalToggleSwitchDriver::onExti() {
    uint32_t now = HAL_GetTick();
    if ((now - last_irq_tick_) < debounce_ms_) return;
    last_irq_tick_ = now;
    pending_ = true;
}

void InternalToggleSwitchDriver::setOutputFlag(bool on) {
    output_on_ = on;
}

} // namespace app
