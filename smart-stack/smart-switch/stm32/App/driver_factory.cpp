#include "driver_factory.hpp"
#include "drivers/rgb_led_driver.hpp"
#include "drivers/internal_toggle_switch_driver.hpp"

namespace app {

IDriver* createDriver(uint16_t id) {
    switch (id) {
    case DRV_RGB_LED: {
        static RgbLedDriver s_rgb;
        return &s_rgb;
    }
    case DRV_INTERNAL_TOGGLE_SWITCH: {
        static InternalToggleSwitchDriver s_sw;
        return &s_sw;
    }
    default:
        return nullptr;
    }
}

} // namespace app
