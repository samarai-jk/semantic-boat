#include "services/internal_toggle_ui.hpp"
#include "service.hpp"
#include "events.hpp"
#include "event_bus.hpp"
#include "drivers/rgb_led_driver.hpp"
#include "drivers/ui_hardware_driver.hpp"
#include "drivers/device_driver.hpp"
#include "driver_factory.hpp"
#include "driver_manager.hpp"

namespace app
{

    RgbLedDriver &InternalToggleUi::GetRgbLedDriver() const
    {
        return *static_cast<RgbLedDriver*>(
            DriverManager::instance().get(DRV_RGB_LED)
        );
    }

    UiHardwareDriver &InternalToggleUi::GetUiHardwareDriver() const
    {
        return *static_cast<UiHardwareDriver*>(
            DriverManager::instance().get(DRV_UI_HARDWARE));
    }

    DeviceDriver &InternalToggleUi::GetDeviceDriver() const
    {
        return *static_cast<DeviceDriver*>(DriverManager::instance().get(DRV_DEVICE));
    }

    void InternalToggleUi::init()
    {

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
            },
            this
        );
    }

    void InternalToggleUi::run() { 
        
        GetUiHardwareDriver().setIntFeedbackLedState(int_feedback_led_);
        GetUiHardwareDriver().setExtFeedbackOutputState(ext_feedback_output_);
        GetUiHardwareDriver().setExtErrorOutputState(ext_error_output_);

        auto device_pos_in = GetDeviceDriver().vsIn();
        auto device_pos_out = GetDeviceDriver().vsOut();

        if (!device_pos_in_.has_value() 
            || device_pos_in != device_pos_in_ 
            || device_pos_out != device_pos_out_)
        {
            if (device_pos_out)
            {
                GetRgbLedDriver().setColor(0.0, 0.1, 0.0);
                GetRgbLedDriver().setProgram(app::RgbLedDriver::Program::Blink, 2, 5);
            }
            else if (device_pos_in)
            {
                GetRgbLedDriver().setColor(0.0, 0.05, 0.0);
                GetRgbLedDriver().setProgram(app::RgbLedDriver::Program::Blink, 3, 90);
            }
            else
            {
                GetRgbLedDriver().setColor(0.0, 0.001, 0.0);
                GetRgbLedDriver().setProgram(app::RgbLedDriver::Program::Static, 0, 0);
            }
            device_pos_in_ = device_pos_in;
            device_pos_out_ = device_pos_out;
        }
    }

} // namespace app
