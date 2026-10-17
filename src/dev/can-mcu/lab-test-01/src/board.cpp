#include "board.hpp"
#include "can_mcu/board.hpp"
#include <cstddef>
#include <cstdint>
extern "C" {
#include "main.h"
extern TIM_HandleTypeDef htim1;
}

namespace lab_test_01::board {
namespace {

// --- Isolated input module digital outputs -----------------------------------

struct InputPin {
    GPIO_TypeDef* port;
    std::uint16_t pin;
};

InputPin moduleInputPins[]{
    {GPIOA, GPIO_PIN_4},
    {GPIOA, GPIO_PIN_5},
    {GPIOA, GPIO_PIN_6},
    {GPIOA, GPIO_PIN_7},
};

bool configureModuleInput(void* context) {
    const auto& input = *static_cast<InputPin*>(context);
    GPIO_InitTypeDef gpio{};
    gpio.Pin = input.pin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(input.port, &gpio);
    return true;
}

bool readModuleInput(void* context) {
    const auto& input = *static_cast<InputPin*>(context);
    return HAL_GPIO_ReadPin(input.port, input.pin) == GPIO_PIN_SET;
}

// --- TIM1: status LEDs and buzzer ------------------------------------------
//
// CubeMX configures PWM mode 1 with low polarity on CH1/CH2 (active-low LEDs,
// so duty is brightness) and high polarity on CH3 (low-side buzzer driver).
// CCR preload is enabled, so compare changes take effect at the next update.

struct PwmChannel {
    std::uint32_t channel;
    float duty;
};

PwmChannel greenChannel{TIM_CHANNEL_1, 0.0f};
PwmChannel redChannel{TIM_CHANNEL_2, 0.0f};
PwmChannel buzzerChannel{TIM_CHANNEL_3, 0.0f};
PwmChannel* const channels[]{&greenChannel, &redChannel, &buzzerChannel};

// Timer frequency while no tone has been played: the buzzer's resonance, so
// the first tone does not change the LED PWM frequency.
constexpr std::uint32_t initialFrequencyHz = 2700u;
bool timerStarted{};

std::uint32_t timerClock() {
    auto clock = HAL_RCC_GetPCLK2Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE2) != RCC_CFGR_PPRE2_DIV1) clock *= 2u;
    return clock;
}

void applyCompare(const PwmChannel& pwm) {
    const auto period = __HAL_TIM_GET_AUTORELOAD(&htim1) + 1u;
    const auto compare = static_cast<std::uint32_t>(static_cast<float>(period) * pwm.duty + 0.5f);
    __HAL_TIM_SET_COMPARE(&htim1, pwm.channel, compare);
}

bool setFrequency(std::uint32_t frequencyHz) {
    if (frequencyHz == 0u) return false;
    const auto clock = timerClock();
    const auto ticks = clock / frequencyHz;
    if (ticks < 2u) return false;
    const auto prescaler = (ticks - 1u) / 65536u;
    if (prescaler > 0xffffu) return false;
    const auto period = clock / ((prescaler + 1u) * frequencyHz);
    __HAL_TIM_SET_PRESCALER(&htim1, prescaler);
    __HAL_TIM_SET_AUTORELOAD(&htim1, period - 1u);
    for (const auto* pwm : channels) applyCompare(*pwm);
    // Load the new prescaler and compares immediately.
    htim1.Instance->EGR = TIM_EGR_UG;
    return true;
}

bool startTimer(void*) {
    if (timerStarted) return true;
    if (!setFrequency(initialFrequencyHz)) return false;
    for (const auto* pwm : channels) {
        if (HAL_TIM_PWM_Start(&htim1, pwm->channel) != HAL_OK) return false;
    }
    timerStarted = true;
    return true;
}

void writeLed(void* context, float duty) {
    auto& pwm = *static_cast<PwmChannel*>(context);
    pwm.duty = duty;
    applyCompare(pwm);
}

void stopLed(void* context) {
    writeLed(context, 0.0f);
}

bool startTone(void*, std::uint32_t frequencyHz, float level) {
    // 50 % duty is full volume for the passive buzzer.
    buzzerChannel.duty = 0.5f * level;
    return setFrequency(frequencyHz);
}

// Interrupt-safe: may run from SysTick via ToneDriver::onTick().
void stopTone(void*) {
    buzzerChannel.duty = 0.0f;
    __HAL_TIM_SET_COMPARE(&htim1, buzzerChannel.channel, 0u);
}

} // namespace

std::array<slstm32::drivers::ButtonConfig, buttonCount> buttons() {
    using can_mcu::ButtonFrontend;
    return {{
        can_mcu::buttonFrontend(ButtonFrontend::fun0, 0u),
        can_mcu::buttonFrontend(ButtonFrontend::fun1, 1u),
        can_mcu::buttonFrontend(ButtonFrontend::fun2, 2u),
        can_mcu::buttonFrontend(ButtonFrontend::fun3, 3u),
        can_mcu::buttonFrontend(ButtonFrontend::fun4, 4u),
        can_mcu::buttonFrontend(ButtonFrontend::fun5, 5u),
    }};
}

void configureLightningIrq() {
    GPIO_InitTypeDef gpio{};
    gpio.Pin = lightningIrqPin;
    gpio.Mode = GPIO_MODE_IT_RISING;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &gpio);
    // Above the buttons: tuning counts edges at 31 kHz.
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 4u, 0u);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

std::array<slstm32::drivers::DigitalInputHardware, 4> isolatedModuleInputs() {
    std::array<slstm32::drivers::DigitalInputHardware, 4> result{};
    // PA4 is the 1-Wire bus and PA6 the lightning sensor IRQ of the sensor
    // tests: DI0 and DI2 stay unconnected.
    for (std::size_t index = 1; index < result.size(); ++index) {
        if (index == 2u) continue;
        result[index] = {&moduleInputPins[index], &configureModuleInput, &readModuleInput, true};
    }
    return result;
}

slstm32::drivers::PwmOutputHardware greenLed() {
    return {&greenChannel, &startTimer, nullptr, &stopLed, &writeLed};
}

slstm32::drivers::PwmOutputHardware redLed() {
    return {&redChannel, &startTimer, nullptr, &stopLed, &writeLed};
}

slstm32::drivers::ToneOutputHardware buzzer() {
    return {&buzzerChannel, &startTimer, &startTone, &stopTone};
}

} // namespace lab_test_01::board
