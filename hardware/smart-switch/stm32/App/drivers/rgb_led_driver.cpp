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
    }
}

void RgbLedDriver::setColor(float r, float g, float b) {
    auto clamp01 = [](float x) -> float {
        if (x < 0.0f) return 0.0f;
        if (x > 1.0f) return 1.0f;
        return x;
    };
    float rf = clamp01(r);
    float gf = clamp01(g);
    float bf = clamp01(b);
    uint32_t vmax = (LED_RESOLUTION > 0) ? (LED_RESOLUTION - 1) : 1;
    // round to nearest (linear mapping)
    r_ = static_cast<uint16_t>(rf * vmax + 0.5f);
    g_ = static_cast<uint16_t>(gf * vmax + 0.5f);
    b_ = static_cast<uint16_t>(bf * vmax + 0.5f);
    // Guarantee at least one logical step for any non-zero request
    if (rf > 0.0f && r_ == 0) r_ = 1;
    if (gf > 0.0f && g_ == 0) g_ = 1;
    if (bf > 0.0f && b_ == 0) b_ = 1;
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

void RgbLedDriver::applyPwm(uint16_t r, uint16_t g, uint16_t b) {
    // Scale 0..LED_RESOLUTION-1 to 0..ARR and write CCRs directly
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim4);
    auto to_ticks = [arr](uint16_t v) -> uint32_t {
        if (v == 0) return 0;
        uint32_t vmax = (LED_RESOLUTION > 0) ? (LED_RESOLUTION - 1) : 1;
        if (v > vmax) v = static_cast<uint16_t>(vmax);
        uint32_t ticks = (static_cast<uint32_t>(v) * arr + (vmax / 2)) / vmax;
        if (ticks == 0) ticks = 1; // ensure at least one tick for non-zero
        return ticks;
    };

    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, to_ticks(r));
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, to_ticks(g));
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, to_ticks(b));
}

} // namespace app
