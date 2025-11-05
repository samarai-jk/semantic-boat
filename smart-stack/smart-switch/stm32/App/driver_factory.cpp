#include "driver_factory.hpp"
#include "drivers/rgb_led_driver.hpp"
#include "drivers/ui_hardware_driver.hpp"
#include "drivers/device_driver.hpp"
#include "drivers/temp_sense_enable_driver.hpp"
#include "drivers/dev_temp_sense_input_driver.hpp"
#include "drivers/current_sense_enable_driver.hpp"
#include "drivers/current_sense_input_driver.hpp"
#include "drivers/latching_relay_driver.hpp"
#include "drivers/voltage_sense_input_driver.hpp"

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
        case DRV_CUR_SENSE_EN:
        {
            static CurrentSenseEnableDriver s_cs_en;
            return &s_cs_en;
        }
        case DRV_CUR_SENSE_IN:
        {
            static CurrentSenseInputDriver s_cs_in;
            return &s_cs_in;
        }
        case DRV_LATCHING_RELAY:
        {
            static LatchingRelayDriver s_latch;
            return &s_latch;
        }
        case DRV_VOL_SENSE_IN:
        {
            static VoltageSenseInputDriver s_vs_in;
            return &s_vs_in;
        }
        default:
            return nullptr;
        }
    }
    
} // namespace app
