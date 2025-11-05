#ifndef APP_DRIVERS_INTERNAL_TOGGLE_UI_HPP
#define APP_DRIVERS_INTERNAL_TOGGLE_UI_HPP

#include <cstdint>
#include <optional>
#include "service.hpp"

extern "C"
{
#include "main.h"
}

namespace app
{

    class RgbLedDriver;
    class UiHardwareDriver;
    class DeviceDriver;
    class TempSenseEnableDriver;

    class InternalToggleUi : public IService
    {
         
    public: // IService
        void init() override;
        void run() override;
    private:
        
        bool relay_on_{false};
        bool ext_feedback_output_{false};
        bool ext_error_output_{false};
        
        bool dev_temp_sense_enabled_{false};
        
        int32_t dev_temp_sense_value_{0};
        int32_t dev_curr_sense_value_{0};
        int32_t dev_volt_sense_value_{0};
        
        std::optional<bool> device_pos_in_{std::nullopt};
        std::optional<bool> device_pos_out_{std::nullopt};
        
    };

} // namespace app

#endif // APP_DRIVERS_INTERNAL_TOGGLE_UI_HPP