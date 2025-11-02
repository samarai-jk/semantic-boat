#include "driver_factory.hpp"
#include "drivers/rgb_led_driver.hpp"
#include "drivers/ui_hardware_driver.hpp"
#include "drivers/device_driver.hpp"

namespace app {

IDriver* createDriver(uint16_t id) {
    switch (id) {
    case DRV_RGB_LED: {
        static RgbLedDriver s_rgb;
        return &s_rgb;
    }
    case DRV_UI_HARDWARE: {
        static UiHardwareDriver s_sw;
        return &s_sw;
    }
    case DRV_DEVICE: {
        static DeviceDriver s_dev;
        return &s_dev;
    }
    default:
        return nullptr;
    }
}

} // namespace app
