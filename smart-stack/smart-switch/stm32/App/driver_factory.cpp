#include "driver_factory.hpp"
#include "drivers/rgb_led_driver.hpp"
#include "drivers/ui_hardware_driver.hpp"
#include "drivers/device_driver.hpp"
#include "drivers/temp_sense_enable_driver.hpp"
#include "drivers/dev_temp_sense_input_driver.hpp"

namespace app
{

    IDriver *createDriver(uint16_t id)
    {
        switch (id)
        {
        case DRV_RGB_LED:
        {
            static RgbLedDriver s_rgb;
            return &s_rgb;
        }
        case DRV_UI_HARDWARE:
        {
            static UiHardwareDriver s_sw;
            return &s_sw;
        }
        case DRV_DEVICE:
        {
            static DeviceDriver s_dev;
            return &s_dev;
        }
        case DRV_TEMP_SENSE_EN:
        {
            static TempSenseEnableDriver s_ts;
            return &s_ts;
        }
        case DRV_DEV_TEMP_SENSE_IN:
        {
            static DevTempSenseInputDriver s_tsin;
            return &s_tsin;
        }
        default:
            return nullptr;
        }
    }
    
} // namespace app
