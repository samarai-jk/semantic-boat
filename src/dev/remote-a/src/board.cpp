#include "board.hpp"
#include <cstddef>
#include <cstdint>
extern "C" {
#include "main.h"
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim15;
}

namespace remote_a::hardware {
namespace {

struct InputContext {
    GPIO_TypeDef* port;
    std::uint16_t pin;
    IRQn_Type irq;
};

struct OutputContext {
    GPIO_TypeDef* port;
    std::uint16_t pin;
};

struct PwmContext {
    TIM_HandleTypeDef* timer;
    std::uint32_t channel;
};

InputContext buttonContexts[]{
    {BTN_FUNCTION_0_GPIO_Port, BTN_FUNCTION_0_Pin, EXTI0_IRQn},
    {BTN_FUNCTION_1_GPIO_Port, BTN_FUNCTION_1_Pin, EXTI1_IRQn},
    {BTN_FUNCTION_2_GPIO_Port, BTN_FUNCTION_2_Pin, EXTI2_IRQn},
    {BTN_FUNCTION_3_GPIO_Port, BTN_FUNCTION_3_Pin, EXTI4_IRQn},
    {BTN_FUNCTION_4_GPIO_Port, BTN_FUNCTION_4_Pin, EXTI9_5_IRQn},
    {BTN_FUNCTION_5_GPIO_Port, BTN_FUNCTION_5_Pin, EXTI9_5_IRQn},
};
OutputContext signalContext{SIG_LED_0_GPIO_Port, SIG_LED_0_Pin};
PwmContext rgbContexts[]{
    {&htim2, TIM_CHANNEL_1},
    {&htim2, TIM_CHANNEL_3},
    {&htim2, TIM_CHANNEL_4},
};
PwmContext buzzerContext{&htim15, TIM_CHANNEL_2};
bool rgbTimerConfigured{};

void configureTimer(TIM_HandleTypeDef& timer, std::uint32_t timerClock,
                    std::uint32_t targetFrequency, std::uint32_t maximumPeriod) {
    constexpr std::uint32_t timerBaseFrequency = 1000000u;
    auto divider = timerClock / timerBaseFrequency;
    if (divider == 0u) divider = 1u;
    auto prescaler = divider - 1u;
    if (prescaler > 0xffffu) prescaler = 0xffffu;
    const auto actualBase = timerClock / (prescaler + 1u);
    auto ticks = actualBase / targetFrequency;
    if (ticks < 2u) ticks = 2u;
    if (ticks > maximumPeriod) ticks = maximumPeriod;
    __HAL_TIM_DISABLE(&timer);
    __HAL_TIM_SET_PRESCALER(&timer, prescaler);
    __HAL_TIM_SET_AUTORELOAD(&timer, ticks - 1u);
    __HAL_TIM_SET_COUNTER(&timer, 0u);
    HAL_TIM_GenerateEvent(&timer, TIM_EVENTSOURCE_UPDATE);
}

bool configureButton(void* context) {
    auto& button = *static_cast<InputContext*>(context);
    GPIO_InitTypeDef input{};
    input.Pin = button.pin;
    input.Mode = GPIO_MODE_IT_FALLING;
    input.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(button.port, &input);
    HAL_NVIC_SetPriority(button.irq, 5u, 0u);
    HAL_NVIC_EnableIRQ(button.irq);
    return true;
}

bool readInput(void* context) {
    const auto& input = *static_cast<InputContext*>(context);
    return HAL_GPIO_ReadPin(input.port, input.pin) == GPIO_PIN_SET;
}

bool configureSignal(void* context) {
    const auto& output = *static_cast<OutputContext*>(context);
    GPIO_InitTypeDef gpio{};
    gpio.Pin = output.pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(output.port, &gpio);
    return true;
}

void writeOutput(void* context, bool value) {
    const auto& output = *static_cast<OutputContext*>(context);
    HAL_GPIO_WritePin(output.port, output.pin, value ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool configureRgb(void*) {
    if (rgbTimerConfigured) return true;
    auto timerClock = HAL_RCC_GetPCLK1Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1) timerClock *= 2u;
    configureTimer(htim2, timerClock, 1000u, 0xffffffffu);
    rgbTimerConfigured = true;
    return true;
}

bool startPwm(void* context) {
    auto& pwm = *static_cast<PwmContext*>(context);
    return HAL_TIM_PWM_Start(pwm.timer, pwm.channel) == HAL_OK;
}

void stopPwm(void* context) {
    auto& pwm = *static_cast<PwmContext*>(context);
    (void)HAL_TIM_PWM_Stop(pwm.timer, pwm.channel);
}

void writePwm(void* context, float duty) {
    auto& pwm = *static_cast<PwmContext*>(context);
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    const auto period = __HAL_TIM_GET_AUTORELOAD(pwm.timer) + 1u;
    const auto compare = static_cast<std::uint32_t>(static_cast<float>(period) * duty);
    __HAL_TIM_SET_COMPARE(pwm.timer, pwm.channel, compare);
}

bool configureBuzzer(void* context) {
    stopPwm(context);
    writePwm(context, 0.0f);
    return true;
}

bool startTone(void* context, std::uint32_t frequencyHz) {
    auto& pwm = *static_cast<PwmContext*>(context);
    if (frequencyHz == 0u) return false;
    auto timerClock = HAL_RCC_GetPCLK2Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE2) != RCC_CFGR_PPRE2_DIV1) timerClock *= 2u;
    configureTimer(*pwm.timer, timerClock, frequencyHz, 0xffffu);
    writePwm(context, 0.5f);
    return startPwm(context);
}

void stopTone(void* context) {
    stopPwm(context);
    writePwm(context, 0.0f);
}

} // namespace

std::array<slstm32::drivers::ButtonConfig, 6> buttons() {
    std::array<slstm32::drivers::ButtonConfig, 6> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = {
            static_cast<std::uint8_t>(index),
            buttonContexts[index].pin,
            {&buttonContexts[index], &configureButton, &readInput, false},
            slstm32::drivers::ButtonInterrupt::pressed,
        };
    }
    return result;
}

slstm32::drivers::DigitalOutputHardware signalLed() {
    return {&signalContext, &configureSignal, &writeOutput, true};
}

std::array<slstm32::drivers::PwmOutputHardware, 3> rgbLed() {
    return {{
        {&rgbContexts[0], &configureRgb, &startPwm, &stopPwm, &writePwm},
        {&rgbContexts[1], &configureRgb, &startPwm, &stopPwm, &writePwm},
        {&rgbContexts[2], &configureRgb, &startPwm, &stopPwm, &writePwm},
    }};
}

slstm32::drivers::ToneOutputHardware buzzer() {
    return {&buzzerContext, &configureBuzzer, &startTone, &stopTone};
}

} // namespace remote_a::hardware
