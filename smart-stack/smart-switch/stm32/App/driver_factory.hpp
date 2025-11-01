#ifndef APP_DRIVER_FACTORY_HPP
#define APP_DRIVER_FACTORY_HPP

#include <cstdint>
#include "driver.hpp"

namespace app {

// Known driver IDs (organizational, not plugins)
enum : uint16_t {
    DRV_RGB_LED = 1,
    DRV_INTERNAL_TOGGLE_SWITCH = 2,
    // Add more here
};

// Factory creates instances of drivers (owned statically in their units)
IDriver* createDriver(uint16_t id);

} // namespace app

#endif // APP_DRIVER_FACTORY_HPP