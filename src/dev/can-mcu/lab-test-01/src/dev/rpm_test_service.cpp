// DEVELOPMENT AND TEST CODE: compiled only with SB_DEV_TOOLS (AGENTS.md,
// "Firmware code categories and build profiles"). Never reference this from
// product code.
#include "rpm_test_service.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "can_mcu/board.hpp"
extern "C" {
#include "main.h"
}

namespace lab_test_01 {

namespace {

using slstm32::LogLevel;

constexpr std::uint16_t inputPin = GPIO_PIN_5; // PA5 = OUT_D_1
constexpr std::uint32_t reportMs = 1000u;
constexpr std::uint32_t sweepStepMs = 3000u;
constexpr std::uint32_t sweepHz[]{5, 10, 20, 50, 75, 100, 150, 200, 300, 500, 750, 1000};

// Edge statistics of the current window (written by the interrupt).
struct Edges {
    volatile std::uint32_t falling{};      // MCU pin high -> low (12 V appears: the module inverts)
    volatile std::uint32_t rising{};
    volatile std::uint32_t firstFallAt{};  // cycle counter
    volatile std::uint32_t lastFallAt{};
    volatile std::uint32_t lastEdgeAt{};
    volatile std::uint64_t lowCycles{};    // time the pin was low (12 V present)
    volatile std::uint64_t highCycles{};
    volatile std::uint32_t minPeriod{0xFFFFFFFFu};
    volatile std::uint32_t maxPeriod{};
};
Edges edges;

void resetEdges() {
    __disable_irq();
    edges.falling = edges.rising = 0u;
    edges.lowCycles = edges.highCycles = 0u;
    edges.minPeriod = 0xFFFFFFFFu;
    edges.maxPeriod = 0u;
    __enable_irq();
}

void setupGenerator() {
    // PA0 = TIM2_CH1 (AF1), push-pull.
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio{};
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &gpio);
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM2EN;
    (void)RCC->APB1ENR1;
    TIM2->CR1 = 0u;
    TIM2->PSC = 79u; // 1 MHz tick
    TIM2->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE; // PWM mode 1
    TIM2->CCER = 0u;
}

void setupCounter() {
    GPIO_InitTypeDef gpio{};
    gpio.Pin = inputPin;
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 3u, 0u);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void formatMilliHz(char (&text)[16], std::uint32_t milliHz) {
    std::snprintf(text, sizeof text, "%lu.%01lu", static_cast<unsigned long>(milliHz / 1000u),
                  static_cast<unsigned long>(milliHz % 1000u / 100u));
}

} // namespace

void RpmTestService::onEdge(std::uint16_t pin) {
    if (pin != inputPin) return;
    const auto now = can_mcu::cycleCounter();
    const bool high = (GPIOA->IDR & inputPin) != 0u;
    const auto since = now - edges.lastEdgeAt;
    if (high) {
        // Rising at the MCU: the low phase (12 V present) ended.
        if (edges.falling != 0u) edges.lowCycles = edges.lowCycles + since;
        edges.rising = edges.rising + 1u;
    } else {
        if (edges.falling != 0u) {
            edges.highCycles = edges.highCycles + since;
            const auto period = now - edges.lastFallAt;
            if (period < edges.minPeriod) edges.minPeriod = period;
            if (period > edges.maxPeriod) edges.maxPeriod = period;
        } else {
            edges.firstFallAt = now;
        }
        edges.lastFallAt = now;
        edges.falling = edges.falling + 1u;
    }
    edges.lastEdgeAt = now;
}

bool RpmTestService::init() {
    setupGenerator();
    setupCounter();
    reportAt_ = windowStart_ = runtime_.millis() + reportMs;
    log_.write(LogLevel::warning, "rpm test: generator TIM2 PWM on PA0, counter on PA5 (OUT_D_1)");
    return console_.add({"rpm", "rpm [gen <Hz> [duty %] | off | sweep [duty %]]  12v-ad-input-iso frequency test (dev, temporary)",
                         &command, this});
}

void RpmTestService::generate(std::uint32_t milliHz, std::uint8_t duty) {
    milliHz_ = milliHz;
    duty_ = duty;
    if (milliHz == 0u) {
        TIM2->CR1 = 0u;
        TIM2->CCER = 0u;
        GPIOA->BRR = GPIO_PIN_0;
        return;
    }
    // 1 MHz tick: period in microseconds.
    const std::uint32_t period = static_cast<std::uint32_t>(1000000000ull / milliHz);
    TIM2->CR1 = 0u;
    TIM2->ARR = period - 1u;
    TIM2->CCR1 = period * duty / 100u;
    TIM2->CNT = 0u;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CCER = TIM_CCER_CC1E;
    TIM2->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
    resetEdges();
    windowStart_ = runtime_.millis();
    reportAt_ = windowStart_ + reportMs;
}

void RpmTestService::report(std::uint32_t now) {
    struct {
        std::uint32_t falling, firstFallAt, lastFallAt, minPeriod, maxPeriod;
        std::uint64_t lowCycles, highCycles;
    } snapshot;
    __disable_irq();
    snapshot.falling = edges.falling;
    snapshot.firstFallAt = edges.firstFallAt;
    snapshot.lastFallAt = edges.lastFallAt;
    snapshot.minPeriod = edges.minPeriod;
    snapshot.maxPeriod = edges.maxPeriod;
    snapshot.lowCycles = edges.lowCycles;
    snapshot.highCycles = edges.highCycles;
    __enable_irq();
    resetEdges();
    const auto windowMs = now - windowStart_;
    windowStart_ = now;
    const std::uint32_t pulses = snapshot.falling;
    std::uint32_t measuredMilliHz = 0u;
    if (pulses >= 2u) {
        const auto span = snapshot.lastFallAt - snapshot.firstFallAt;
        measuredMilliHz = static_cast<std::uint32_t>(static_cast<std::uint64_t>(pulses - 1u) * can_mcu::coreClockHz *
                                                     1000u / (span ? span : 1u));
    }
    const auto expected = static_cast<std::int32_t>(static_cast<std::uint64_t>(milliHz_) * windowMs / 1000000u);
    const auto total = snapshot.lowCycles + snapshot.highCycles;
    const unsigned lowPercent = total ? static_cast<unsigned>(snapshot.lowCycles * 100u / total) : 0u;
    char generated[16];
    char measured[16];
    formatMilliHz(generated, milliHz_);
    formatMilliHz(measured, measuredMilliHz);
    const auto toMicros = [](std::uint32_t cycles) { return static_cast<unsigned long>(cycles / (can_mcu::coreClockHz / 1000000u)); };
    log_.writef(LogLevel::info,
                "rpm: gen %s Hz %u %% | measured %s Hz, %lu pulses (expected %ld, diff %ld), 12 V %u %% of the time, "
                "period %lu..%lu us",
                generated, static_cast<unsigned>(duty_), measured, static_cast<unsigned long>(pulses),
                static_cast<long>(expected), static_cast<long>(static_cast<std::int32_t>(pulses) - expected), lowPercent,
                snapshot.minPeriod == 0xFFFFFFFFu ? 0ul : toMicros(snapshot.minPeriod), toMicros(snapshot.maxPeriod));
}

void RpmTestService::run() {
    const auto now = runtime_.millis();
    if (sweeping_ && static_cast<std::int32_t>(now - sweepNextAt_) >= 0) {
        if (sweepStep_ >= sizeof sweepHz / sizeof sweepHz[0]) {
            sweeping_ = false;
            generate(0u, duty_);
            log_.write(LogLevel::info, "rpm: sweep done");
        } else {
            generate(sweepHz[sweepStep_++] * 1000u, duty_);
            // Skip the first second after a change (settling), report the next two.
            windowStart_ = now;
            reportAt_ = now + reportMs;
            sweepNextAt_ = now + sweepStepMs;
        }
    }
    if (static_cast<std::int32_t>(now - reportAt_) < 0) return;
    reportAt_ += reportMs;
    if (milliHz_ != 0u) {
        report(now);
    } else {
        resetEdges();
        windowStart_ = now;
    }
}

void RpmTestService::command(void* context, std::size_t argc, const char* const* argv) {
    auto& self = *static_cast<RpmTestService*>(context);
    if (argc >= 3u && std::strcmp(argv[1], "gen") == 0) {
        const auto hz = std::strtod(argv[2], nullptr);
        const auto duty = argc >= 4u ? std::strtoul(argv[3], nullptr, 10) : 50u;
        if (hz >= 0.5 && hz <= 20000.0 && duty >= 1u && duty <= 99u) {
            self.sweeping_ = false;
            self.generate(static_cast<std::uint32_t>(hz * 1000.0 + 0.5), static_cast<std::uint8_t>(duty));
            return;
        }
    } else if (argc == 2u && std::strcmp(argv[1], "off") == 0) {
        self.sweeping_ = false;
        self.generate(0u, self.duty_);
        self.log_.write(LogLevel::info, "rpm: generator off");
        return;
    } else if ((argc == 2u || argc == 3u) && std::strcmp(argv[1], "sweep") == 0) {
        const auto duty = argc == 3u ? std::strtoul(argv[2], nullptr, 10) : 50u;
        if (duty >= 1u && duty <= 99u) {
            self.duty_ = static_cast<std::uint8_t>(duty);
            self.sweeping_ = true;
            self.sweepStep_ = 0u;
            self.sweepNextAt_ = self.runtime_.millis();
            self.log_.writef(LogLevel::info, "rpm: sweep 5..1000 Hz at %u %% duty, 3 s per step",
                             static_cast<unsigned>(duty));
            return;
        }
    }
    self.log_.write(LogLevel::warning, "usage: rpm [gen <Hz> [duty %] | off | sweep [duty %]]");
}

} // namespace lab_test_01
