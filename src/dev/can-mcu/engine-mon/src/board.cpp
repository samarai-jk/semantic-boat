#include "board.hpp"
#include "can_mcu/board.hpp"
extern "C" {
#include "main.h"
extern TIM_HandleTypeDef htim1;
}

namespace engine_mon::board {
namespace {

// --- Digital inputs -----------------------------------------------------------

struct InputPin {
    GPIO_TypeDef* port;
    std::uint16_t pin;
};

InputPin inputPins[digitalInputCount]{
    {GPIOA, GPIO_PIN_0},  // DI0, TIM2_CH1: rpm
    {GPIOA, GPIO_PIN_1},  // DI1, TIM2_CH2
    {GPIOA, GPIO_PIN_3},  // DI2
    {GPIOA, GPIO_PIN_7},  // DI3
    {GPIOB, GPIO_PIN_4},  // DI4
    {GPIOB, GPIO_PIN_5},  // DI5
    {GPIOC, GPIO_PIN_13}, // DI6
    {GPIOA, GPIO_PIN_15}, // DI7
};

bool configureInput(void* context) {
    const auto& input = *static_cast<InputPin*>(context);
    GPIO_InitTypeDef gpio{};
    gpio.Pin = input.pin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(input.port, &gpio);
    return true;
}

bool readInput(void* context) {
    const auto& input = *static_cast<InputPin*>(context);
    return HAL_GPIO_ReadPin(input.port, input.pin) == GPIO_PIN_SET;
}

// --- TIM1: digital outputs ------------------------------------------------------
//
// PWM mode 1, high polarity on CH1..CH3. The channels are configured here as
// well, so a different CubeMX setting (a forced output keeps a load on) can
// never switch a load.

constexpr std::uint32_t outputFrequencyHz = 1000u;

struct OutputChannel {
    std::uint32_t channel;
};

OutputChannel outputChannels[outputCount]{{TIM_CHANNEL_1}, {TIM_CHANNEL_2}, {TIM_CHANNEL_3}};
bool timerConfigured{};

bool configureTimer() {
    if (timerConfigured) return true;
    auto clock = HAL_RCC_GetPCLK2Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE2) != RCC_CFGR_PPRE2_DIV1) clock *= 2u;
    const auto ticks = clock / outputFrequencyHz;
    const auto prescaler = (ticks - 1u) / 65536u;
    __HAL_TIM_SET_PRESCALER(&htim1, prescaler);
    __HAL_TIM_SET_AUTORELOAD(&htim1, clock / ((prescaler + 1u) * outputFrequencyHz) - 1u);
    TIM_OC_InitTypeDef channel{};
    channel.OCMode = TIM_OCMODE_PWM1;
    channel.Pulse = 0u;
    channel.OCPolarity = TIM_OCPOLARITY_HIGH;
    channel.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    channel.OCFastMode = TIM_OCFAST_DISABLE;
    channel.OCIdleState = TIM_OCIDLESTATE_RESET;
    channel.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    for (const auto& output : outputChannels) {
        if (HAL_TIM_PWM_ConfigChannel(&htim1, &channel, output.channel) != HAL_OK) return false;
    }
    htim1.Instance->EGR = TIM_EGR_UG;
    timerConfigured = true;
    return true;
}

bool configureOutput(void*) { return configureTimer(); }

bool startOutput(void* context) {
    const auto& output = *static_cast<OutputChannel*>(context);
    return HAL_TIM_PWM_Start(&htim1, output.channel) == HAL_OK;
}

void writeOutput(void* context, float duty) {
    const auto& output = *static_cast<OutputChannel*>(context);
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    const auto period = __HAL_TIM_GET_AUTORELOAD(&htim1) + 1u;
    // Duty 1 sets the compare beyond the period: the output stays high.
    const auto compare = duty >= 1.0f ? period : static_cast<std::uint32_t>(static_cast<float>(period) * duty + 0.5f);
    __HAL_TIM_SET_COMPARE(&htim1, output.channel, compare);
}

void stopOutput(void* context) {
    writeOutput(context, 0.0f);
}

} // namespace

std::array<slstm32::drivers::ButtonConfig, buttonCount> buttons() {
    using can_mcu::ButtonFrontend;
    return {{
        can_mcu::buttonFrontend(ButtonFrontend::fun4, 0u),
        can_mcu::buttonFrontend(ButtonFrontend::fun5, 1u),
    }};
}

std::array<slstm32::drivers::DigitalInputHardware, digitalInputCount> digitalInputs() {
    std::array<slstm32::drivers::DigitalInputHardware, digitalInputCount> result{};
    for (std::size_t index = 0u; index < result.size(); ++index) {
        result[index] = {&inputPins[index], &configureInput, &readInput, true};
    }
    return result;
}

std::uint16_t digitalInputMask(std::size_t index) {
    return index < digitalInputCount ? inputPins[index].pin : 0u;
}

std::array<slstm32::drivers::PwmOutputHardware, outputCount> outputs() {
    std::array<slstm32::drivers::PwmOutputHardware, outputCount> result{};
    for (std::size_t index = 0u; index < result.size(); ++index) {
        result[index] = {&outputChannels[index], &configureOutput, &startOutput, &stopOutput, &writeOutput};
    }
    return result;
}

} // namespace engine_mon::board
