#ifndef APP_DRIVERS_RGB_LED_DRIVER_HPP
#define APP_DRIVERS_RGB_LED_DRIVER_HPP

#include <cstdint>
#include "driver.hpp"

extern "C" {
#include "main.h" // for HAL and htim4
}

namespace app {

// Configure LED resolution (number of discrete steps per channel)
// Values passed to setColor are normalized [0.0f .. 1.0f] and will be
// mapped linearly to the discrete range [0 .. LED_RESOLUTION-1].
constexpr uint16_t LED_RESOLUTION = 4096; // default: 12-bit

// Simple GRB PWM LED driver using TIM4 CH1/CH3/CH4
class RgbLedDriver : public IDriver {
public:
    enum class Program : uint8_t { Static = 0, Blink = 1 };

    void init() override;
    void run() override;

    // Set color by normalized percent per channel (0.0f .. 1.0f)
    // Values are clamped; mapping applies LED_RESOLUTION.
    void setColor(float r, float g, float b);
    // Set LED behavior:
    // - Static: frequency/duty are ignored
    // - Blink:  frequency_hz > 0, duty_percent in [0..100]
    void setProgram(Program p, uint16_t frequency_hz, uint8_t duty_percent);

private:
    void applyPwm(uint16_t r, uint16_t g, uint16_t b);
    

    // current state
    uint16_t r_{0}, g_{0}, b_{0};
    Program prog_{Program::Static};
    // Blink timing (ms)
    uint16_t on_ms_{250};
    uint16_t off_ms_{250};
    uint32_t last_toggle_tick_{0};
    bool blink_on_{true};

    
};

} // namespace app

#endif // APP_DRIVERS_RGB_LED_DRIVER_HPP
