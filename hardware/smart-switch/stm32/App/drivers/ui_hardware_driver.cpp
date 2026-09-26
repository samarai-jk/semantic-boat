#include "drivers/ui_hardware_driver.hpp"
#include "events.hpp"
#include "event_bus.hpp"
#include "executor.hpp"
#include "log.hpp"
#include "ui_hardware_driver.hpp"

namespace app
{

    void UiHardwareDriver::init()
    {
        HAL_GPIO_WritePin(FEEDBACK_LED_GPIO_Port, FEEDBACK_LED_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MCU_FEEDBACK_GPIO_Port, MCU_FEEDBACK_Pin, GPIO_PIN_RESET);
    }

    void UiHardwareDriver::run()
    {
        
        HAL_GPIO_WritePin(
            FEEDBACK_LED_GPIO_Port,
            FEEDBACK_LED_Pin,
            int_feedback_led_on_ ? GPIO_PIN_SET : GPIO_PIN_RESET);
            
        HAL_GPIO_WritePin(
            MCU_FEEDBACK_GPIO_Port,
            MCU_FEEDBACK_Pin,
            ext_feedback_output_on_ ? GPIO_PIN_SET : GPIO_PIN_RESET);            

        HAL_GPIO_WritePin(
            MCU_ERROR_GPIO_Port,
            MCU_ERROR_Pin,
            ext_error_output_on_ ? GPIO_PIN_SET : GPIO_PIN_RESET);            

        if (int_toggle_pending_)
        {
            uint32_t now = HAL_GetTick();
            if ((now - last_irq_tick_) >= debounce_ms_)
            {
                int_toggle_pending_ = false;
                if (HAL_GPIO_ReadPin(BTN_TOGGLE_GPIO_Port, BTN_TOGGLE_Pin) == GPIO_PIN_RESET) {
                    EventBus::instance().publish(EVT_INTERNAL_TOGGLE_BUTTON_PRESSED);
                }
            }
        }
        
        if (ext_toggle_pending_)
        {
            uint32_t now = HAL_GetTick();
            if ((now - last_irq_tick_) >= debounce_ms_)
            {
                ext_toggle_pending_ = false;
                if (HAL_GPIO_ReadPin(MCU_TOGGLE_GPIO_Port, MCU_TOGGLE_Pin) == GPIO_PIN_RESET) {
                    EventBus::instance().publish(EVT_EXTERNAL_TOGGLE_BUTTON_PRESSED);
                }
            }
        }
        
        if (fun_button_pending_)
        {
            uint32_t now = HAL_GetTick();
            if ((now - last_irq_tick_) >= debounce_ms_)
            {
                fun_button_pending_ = false;
                if (HAL_GPIO_ReadPin(BTN_FUNCTION_GPIO_Port, BTN_FUNCTION_Pin) == GPIO_PIN_RESET) {
                    EventBus::instance().publish(EVT_FUNCTION_BUTTON_PRESSED);
                }
            }
        }
        
    }
    
    void UiHardwareDriver::onInternalToggle()
    {
        uint32_t now = HAL_GetTick();
        if ((now - last_irq_tick_) < debounce_ms_)
            return;
        last_irq_tick_ = now;
        int_toggle_pending_ = true;
    }

    void UiHardwareDriver::onExternalToggle()
    {
        uint32_t now = HAL_GetTick();
        if ((now - last_irq_tick_) < debounce_ms_)
            return;
        last_irq_tick_ = now;
        ext_toggle_pending_ = true;
    }

    void UiHardwareDriver::onFunctionButton()
    {
        uint32_t now = HAL_GetTick();
        if ((now - last_irq_tick_) < debounce_ms_)
            return;
        last_irq_tick_ = now;
        fun_button_pending_ = true;
    }

    void UiHardwareDriver::setIntFeedbackLedState(bool state)
    {
        int_feedback_led_on_ = state;
    }

    void UiHardwareDriver::setExtFeedbackOutputState(bool state)
    {
        ext_feedback_output_on_ = state;
    }

    void UiHardwareDriver::setExtErrorOutputState(bool state)
    {
        ext_error_output_on_ = state;
    }

} // namespace app
