#include "services/internal_toggle_ui.hpp"
#include "service.hpp"
#include "events.hpp"
#include "event_bus.hpp"
#include "driver_store.hpp"
#include "lib.hpp"

namespace app
{
 
    void InternalToggleUi::init()
    {

        getTempSenseEnableDriver().set(!dev_temp_sense_enabled_);
        getDevTempSenseInputDriver().setEnabled(dev_temp_sense_enabled_);
        
        EventBus::instance().subscribe(
            EVT_INTERNAL_TOGGLE_BUTTON_PRESSED,
            [](uint16_t, const void *, uint16_t, void *ctx)
            {
                auto *self = static_cast<InternalToggleUi *>(ctx);
                if (!self)
                    return;
                self->int_feedback_led_ = !self->int_feedback_led_;
            },
            this);

        EventBus::instance().subscribe(
            EVT_EXTERNAL_TOGGLE_BUTTON_PRESSED,
            [](uint16_t, const void *, uint16_t, void *ctx)
            {
                auto *self = static_cast<InternalToggleUi *>(ctx);
                if (!self)
                    return;
                self->ext_feedback_output_ = !self->ext_feedback_output_;
            },
            this
        );
        
        EventBus::instance().subscribe(
            EVT_FUNCTION_BUTTON_PRESSED,
            [](uint16_t, const void *, uint16_t, void *ctx)
            {
                auto *self = static_cast<InternalToggleUi *>(ctx);
                if (!self)
                    return;
                self->ext_error_output_ = !self->ext_error_output_;
                self->dev_temp_sense_enabled_ = !self->dev_temp_sense_enabled_;
                getTempSenseEnableDriver().set(!self->dev_temp_sense_enabled_);
                getDevTempSenseInputDriver().setEnabled(self->dev_temp_sense_enabled_);
            },
            this
        );
    }
    
    void InternalToggleUi::run() { 
        
        dev_temp_sense_value = getDevTempSenseInputDriver().value();
        static uint32_t last_trace_time = 0;
        if (dev_temp_sense_enabled_ && HAL_GetTick() - last_trace_time > 1000)
        {
            last_trace_time = HAL_GetTick();
            float volts = getDevTempSenseInputDriver().valueV();
            char volts_str[10];
            snprintf(volts_str, sizeof(volts_str), "%.3f", volts);
            swo_trace_line(
                LOG_VERBOSE, 
                std::string("dev_temp_sense_value: ") 
                + std::to_string(dev_temp_sense_value)
                + " -> "
                + volts_str
                + "V"
            );
        }
        getUiHardwareDriver().setIntFeedbackLedState(int_feedback_led_);
        getUiHardwareDriver().setExtFeedbackOutputState(ext_feedback_output_);
        getUiHardwareDriver().setExtErrorOutputState(ext_error_output_);

        auto device_pos_in = getDeviceDriver().vsIn();
        auto device_pos_out = getDeviceDriver().vsOut();

        if (!device_pos_in_.has_value() 
            || device_pos_in != device_pos_in_ 
            || device_pos_out != device_pos_out_)
        {
            if (device_pos_out)
            {
                getRgbLedDriver().setColor(0.0, 0.1, 0.0);
                getRgbLedDriver().setProgram(app::RgbLedDriver::Program::Blink, 2, 5);
            }
            else if (device_pos_in)
            {
                getRgbLedDriver().setColor(0.0, 0.05, 0.0);
                getRgbLedDriver().setProgram(app::RgbLedDriver::Program::Blink, 3, 90);
            }
            else
            {
                getRgbLedDriver().setColor(0.0, 0.001, 0.0);
                getRgbLedDriver().setProgram(app::RgbLedDriver::Program::Static, 0, 0);
            }
            device_pos_in_ = device_pos_in;
            device_pos_out_ = device_pos_out;
        }
        
    }

} // namespace app
