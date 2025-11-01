#include "drivers/rgb_led_driver.hpp"

extern "C" {
#include "app.h"
}

// TIM4 is configured in main.c with period 255; use channels 1,3,4
extern TIM_HandleTypeDef htim4;

namespace app {

void RgbLedDriver::init() {
    // Start PWM channels (safe if already started)
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1); // R
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3); // G
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4); // B
    applyPwm(r_, g_, b_);
}

void RgbLedDriver::run() {
    if (prog_ == Program::Blink) {
        uint32_t now = HAL_GetTick();
        uint32_t elapsed = now - last_toggle_tick_;
        uint16_t phase_ms = blink_on_ ? on_ms_ : off_ms_;
        if (elapsed >= phase_ms) {
            last_toggle_tick_ = now;
            blink_on_ = !blink_on_;
            applyPwm(blink_on_ ? r_ : 0, blink_on_ ? g_ : 0, blink_on_ ? b_ : 0);
        }
    } else {
        // Static color; nothing to do repeatedly
    }
}

void RgbLedDriver::setColor(uint8_t r, uint8_t g, uint8_t b) {
    r_ = r; g_ = g; b_ = b;
    if (prog_ == Program::Static || blink_on_) {
        applyPwm(r_, g_, b_);
    }
}

void RgbLedDriver::setProgram(Program p, uint16_t frequency_hz, uint8_t duty_percent) {
    prog_ = p;
    if (prog_ == Program::Blink) {
        if (frequency_hz == 0) frequency_hz = 1; // avoid div by zero
        if (duty_percent > 100) duty_percent = 100;
        uint32_t period_ms = 1000u / frequency_hz; // integer ms
        if (period_ms == 0) period_ms = 1; // cap minimum
        on_ms_ = static_cast<uint16_t>((period_ms * duty_percent) / 100u);
        off_ms_ = static_cast<uint16_t>(period_ms - on_ms_);
        if (on_ms_ == 0 && duty_percent > 0) on_ms_ = 1;
        if (off_ms_ == 0 && duty_percent < 100) off_ms_ = 1;
        last_toggle_tick_ = HAL_GetTick();
        blink_on_ = true;
        applyPwm(r_, g_, b_);
    } else {
        applyPwm(r_, g_, b_);
    }
}

void RgbLedDriver::applyPwm(uint8_t r, uint8_t g, uint8_t b) {
    // Channels mapping: App/app.c used CH1, CH3, CH4; keep same
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, r);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, g);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, b);
}

} // namespace app
