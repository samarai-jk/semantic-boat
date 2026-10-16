#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/hardware.hpp"
#include "slstm32/runtime.hpp"

namespace slstm32::drivers {

// Dimmable single-color LED on a PWM output. It offers the same steady, pulse,
// and blink modes as LedDriver; brightness is the normalized duty used while
// the LED is on and may change at any time without disturbing the active mode.
// Output polarity (active-low LEDs) belongs in the PWM binding or timer setup.
class PwmLedDriver final : public Driver {
public:
    PwmLedDriver(Runtime runtime, PwmOutputHardware hardware, float brightness = 1.0f)
        : runtime_(runtime), hardware_(hardware), brightness_(clamp(brightness)) {}

    bool init() override;
    void run() override;
    void set(bool value);
    void on() { set(true); }
    void off() { set(false); }
    void toggle() { set(!value_); }
    void pulse(std::uint32_t durationMs);
    void blink(std::uint32_t periodMs, std::uint32_t onTimeMs);
    void setBrightness(float brightness);
    float brightness() const { return brightness_; }
    bool value() const { return value_; }

private:
    enum class Mode : std::uint8_t { steady, pulse, blink };
    static float clamp(float value);
    void write(bool value);

    Runtime runtime_;
    PwmOutputHardware hardware_;
    float brightness_{};
    Mode mode_{Mode::steady};
    bool value_{};
    std::uint32_t deadline_{};
    std::uint32_t periodMs_{};
    std::uint32_t onTimeMs_{};
};

} // namespace slstm32::drivers
