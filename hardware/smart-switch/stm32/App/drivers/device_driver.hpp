#ifndef APP_DRIVERS_DEVICE_DRIVER_HPP
#define APP_DRIVERS_DEVICE_DRIVER_HPP

#include <cstdint>
#include "driver.hpp"

extern "C" {
#include "main.h"
}

namespace app {

// Simple device driver that samples two digital inputs on PA5/PA6
// (MCU_DIG_DEV_VS_IN, MCU_DIG_DEV_VS_OUT) and stores their current states.
class DeviceDriver : public IDriver {
public:
    void init() override;
    void run() override;

    // Getters for the most recently sampled states
    bool vsIn() const { return vs_in_; }
    bool vsOut() const { return vs_out_; }

private:
    bool vs_in_{false};
    bool vs_out_{false};
};

} // namespace app

#endif // APP_DRIVERS_DEVICE_DRIVER_HPP
