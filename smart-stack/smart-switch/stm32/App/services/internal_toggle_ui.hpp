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

    class InternalToggleUi : public IService
    {
    public:
        
        RgbLedDriver& GetRgbLedDriver() const; 
        UiHardwareDriver& GetUiHardwareDriver() const; 
        DeviceDriver& GetDeviceDriver() const; 

    public: // IService
        void init() override;
        void run() override;
    private:
        
        bool int_feedback_led_{false};
        bool ext_feedback_output_{false};
        bool ext_error_output_{false};
        
        std::optional<bool> device_pos_in_{std::nullopt};
        std::optional<bool> device_pos_out_{std::nullopt};
        
    };

} // namespace app

#endif // APP_DRIVERS_INTERNAL_TOGGLE_UI_HPP