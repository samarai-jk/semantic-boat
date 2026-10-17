#include "can_mcu/dac_audio.hpp"
#include "can_mcu/board.hpp"
extern "C" {
#include "main.h"
}

namespace can_mcu {

namespace {
DacAudio* active = nullptr;
constexpr std::uint32_t irqPriority = 6u; // below the buttons and the CAN bus
} // namespace

void dacAudioSample() {
    if (!active) return;
    // 12-bit DAC around mid scale.
    const auto value = static_cast<std::uint32_t>(2048 + (active->synth_.nextSample() >> 4));
    if (active->pin_ == DacAudio::Pin::pa4) {
        DAC1->DHR12R1 = value;
    } else {
        DAC1->DHR12R2 = value;
    }
}

bool DacAudio::start() {
    const auto rate = synth_.sampleRate();
    if (rate < 1000u || rate > 48000u) return false;
    const std::uint32_t pinIndex = pin_ == Pin::pa4 ? 4u : 5u;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIOA->MODER |= 3u << (pinIndex * 2u);    // analog
    GPIOA->PUPDR &= ~(3u << (pinIndex * 2u));
    RCC->APB1ENR1 |= RCC_APB1ENR1_DAC1EN;
    (void)RCC->APB1ENR1;
    if (pin_ == Pin::pa4) {
        DAC1->MCR &= ~DAC_MCR_MODE1; // normal mode, buffer on, pin output
        DAC1->CR |= DAC_CR_EN1;
        DAC1->DHR12R1 = 2048u;
    } else {
        DAC1->MCR &= ~DAC_MCR_MODE2;
        DAC1->CR |= DAC_CR_EN2;
        DAC1->DHR12R2 = 2048u;
    }
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;
    (void)RCC->APB1ENR1;
    // TIM6 runs on the APB1 timer clock (HCLK with APB1 undivided).
    TIM6->PSC = 0u;
    TIM6->ARR = SystemCoreClock / rate - 1u;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0u;
    TIM6->DIER = TIM_DIER_UIE;
    active = this;
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, irqPriority, 0u);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    TIM6->CR1 = TIM_CR1_CEN;
    return true;
}

void DacAudio::stop() {
    TIM6->CR1 = 0u;
    HAL_NVIC_DisableIRQ(TIM6_DAC_IRQn);
    if (active == this) active = nullptr;
}

} // namespace can_mcu

extern "C" void TIM6_DAC_IRQHandler(void) {
    TIM6->SR = 0u;
    can_mcu::dacAudioSample();
}
