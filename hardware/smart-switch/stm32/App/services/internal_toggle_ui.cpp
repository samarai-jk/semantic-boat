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
        
        // temp sense enable
        getTempSenseEnableDriver().set(!dev_temp_sense_enabled_);
        
        // temp sense
        getDevTempSenseInputDriver().setEnabled(dev_temp_sense_enabled_);
        getDevTempSenseInputDriver().setAveragingEnabled(true);
        getDevTempSenseInputDriver().setAveragingWindow(50);
        
        // current sense enable
        getCurrentSenseEnableDriver().set(true);
        
        // current sense
        getCurrentSenseInputDriver().setEnabled(true);
        getCurrentSenseInputDriver().setAveragingEnabled(true);
        getCurrentSenseInputDriver().setAveragingWindow(50);
        
        // voltage sense
        getVoltageSenseInputDriver().setEnabled(true);
        
        EventBus::instance().subscribe(
            EVT_INTERNAL_TOGGLE_BUTTON_PRESSED,
            [](uint16_t, const void *, uint16_t, void *ctx)
            {
                auto *self = static_cast<InternalToggleUi *>(ctx);
                if (!self)
                    return;
                self->relay_on_ = !self->relay_on_;
                if (self->relay_on_) {
                    getLatchingRelayDriver().switchOn();
                } else {
                    getLatchingRelayDriver().switchOff();
                }
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
        
        static uint32_t last_trace_time = 0;
        if (HAL_GetTick() - last_trace_time > 1000)
        {
            
            std::string temp{};
            std::string curr{};
            std::string volt{};
            
            last_trace_time = HAL_GetTick();
            if (dev_temp_sense_enabled_) {
                dev_temp_sense_value_ = getDevTempSenseInputDriver().value();
                float volts = getDevTempSenseInputDriver().valueV();
                char volts_str[10];
                snprintf(volts_str, sizeof(volts_str), "%.4f", volts);
                float temp_c = getDevTempSenseInputDriver().temperatureC();
                char temp_str[10];
                snprintf(temp_str, sizeof(temp_str), "%.2f", temp_c);
                temp = std::to_string(dev_temp_sense_value_) 
                    + "/" + volts_str + "V"
                    + "/" + temp_str + "C";
            }
            {
                dev_curr_sense_value_ = getCurrentSenseInputDriver().value();
                float volts = getCurrentSenseInputDriver().valueV();
                auto volts_corrected = (volts / 0.359) - 0.5;
                if (volts_corrected < 0) volts_corrected = 0;
                auto amps = volts_corrected / 0.2;
                char volts_str[10];
                snprintf(volts_str, sizeof(volts_str), "%.4f", volts_corrected);
                char amps_str[10];
                snprintf(amps_str, sizeof(amps_str), "%.4f", amps);
                curr = std::to_string(dev_curr_sense_value_) 
                    + "/" + volts_str + "V" 
                    + "/" + amps_str + "A";
            }       
            {
                dev_volt_sense_value_ = getVoltageSenseInputDriver().value();
                float volts = getVoltageSenseInputDriver().valueV() * 10;
                char volts_str[10];
                snprintf(volts_str, sizeof(volts_str), "%.4f", volts);
                volt = std::to_string(dev_volt_sense_value_) + "/" + volts_str + "V";            
            }  
            
            swo_trace_line("volt=" + volt + ", curr=" + curr + ", temp=" + temp);
            
        }
        getUiHardwareDriver().setIntFeedbackLedState(relay_on_);
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
                getRgbLedDriver().setColor(0.0, 0.02, 0.0);
                getRgbLedDriver().setProgram(app::RgbLedDriver::Program::Blink, 3, 80);
            }
            else if (device_pos_in)
            {
                getRgbLedDriver().setColor(0.0, 0.005, 0.0);
                getRgbLedDriver().setProgram(app::RgbLedDriver::Program::Blink, 3, 10);
            }
            else
            {
                getRgbLedDriver().setColor(0.0, 0.001, 0.0);
                getRgbLedDriver().setProgram(app::RgbLedDriver::Program::Static, 0, 0);
            }
            device_pos_in_.emplace(device_pos_in);
            device_pos_out_.emplace(device_pos_out);
        }
        
    }

} // namespace app
