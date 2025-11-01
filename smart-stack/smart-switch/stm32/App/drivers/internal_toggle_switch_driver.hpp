#ifndef APP_DRIVERS_INTERNAL_TOGGLE_SWITCH_DRIVER_HPP
#define APP_DRIVERS_INTERNAL_TOGGLE_SWITCH_DRIVER_HPP

#include <cstdint>
#include "driver.hpp"

extern "C" {
#include "main.h"
}

namespace app {

class InternalToggleSwitchDriver : public IDriver {
public:
    static inline GPIO_TypeDef* const kBtnPort = BTN_TOGGLE_GPIO_Port; // PB4
    static inline uint16_t const kBtnPin = BTN_TOGGLE_Pin;
    static inline GPIO_TypeDef* const kLedPort = FEEDBACK_LED_GPIO_Port; // PB0
    static inline uint16_t const kLedPin = FEEDBACK_LED_Pin;

    void init() override;
    void run() override;

    void onExti();

    void setOutputFlag(bool on);
    bool outputFlag() const { return output_on_; }

private:
    static constexpr uint16_t debounce_ms_ = 30;
    volatile uint32_t last_irq_tick_{0};
    volatile bool pending_{false};
    bool output_on_{false};
};

} // namespace app

#endif // APP_DRIVERS_INTERNAL_TOGGLE_SWITCH_DRIVER_HPP
