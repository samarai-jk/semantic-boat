#ifndef APP_DRIVERS_INTERNAL_TOGGLE_SWITCH_DRIVER_HPP
#define APP_DRIVERS_INTERNAL_TOGGLE_SWITCH_DRIVER_HPP

#include <cstdint>
#include "driver.hpp"

extern "C" {
#include "main.h"
}

namespace app
{

    class UiHardwareDriver : public IDriver
    {

    public:
    
        void init() override;
        void run() override;

        void onInternalToggle();
        void onExternalToggle();
        void onFunctionButton();

        void setIntFeedbackLedState(bool state);
        bool intFeedbackLedState() const { return int_feedback_led_on_; }

        void setExtFeedbackOutputState(bool state);
        bool extFeedbackOutputState() const { return ext_feedback_output_on_; }

        void setExtErrorOutputState(bool state);
        bool extErrorOutputState() const { return ext_error_output_on_; }

    private:
        
        static constexpr uint16_t debounce_ms_ = 30;
        volatile uint32_t last_irq_tick_{0};
        
        volatile bool int_toggle_pending_{false};
        volatile bool ext_toggle_pending_{false};
        volatile bool fun_button_pending_{false};
        
        bool int_feedback_led_on_{false};
        bool ext_feedback_output_on_{false};
        bool ext_error_output_on_{false};
        
    };

} // namespace app

#endif // APP_DRIVERS_INTERNAL_TOGGLE_SWITCH_DRIVER_HPP
