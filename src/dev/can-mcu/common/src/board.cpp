#include "can_mcu/board.hpp"
#include "can_mcu/fatal.h"
#include <cstddef>
extern "C" {
#include "main.h"
}

namespace can_mcu {
namespace {

// --- Runtime ---------------------------------------------------------------

std::uint32_t millis() { return HAL_GetTick(); }

volatile std::uint32_t criticalDepth{};
std::uint32_t savedPrimask{};

void enterCritical() {
    const auto primask = __get_PRIMASK();
    __disable_irq();
    if (criticalDepth++ == 0u) savedPrimask = primask;
}

void exitCritical() {
    if (criticalDepth != 0u && --criticalDepth == 0u && savedPrimask == 0u) __enable_irq();
}

void enableCycleCounter() {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

// --- ST-Link VCP (USART1, initialized by CubeMX) ---------------------------

bool vcpReady(void*) {
    return (USART1->CR1 & USART_CR1_UE) != 0u && (USART1->CR1 & USART_CR1_TE) != 0u;
}

// Polls TXE instead of using interrupts: the main loop drains the log, and a
// stalled loop only delays output.
std::size_t vcpWrite(void*, const std::uint8_t* data, std::size_t size) {
    std::size_t written = 0u;
    while (written < size && (USART1->ISR & USART_ISR_TXE) != 0u) {
        USART1->TDR = data[written++];
    }
    return written;
}

// --- Button frontends ------------------------------------------------------

struct FrontendInput {
    GPIO_TypeDef* port;
    std::uint16_t pin;
    IRQn_Type irq;
};

// Order matches ButtonFrontend. See can-mcu.md "Button frontends".
FrontendInput frontendInputs[]{
    {GPIOC, GPIO_PIN_13, EXTI15_10_IRQn},
    {GPIOA, GPIO_PIN_15, EXTI15_10_IRQn},
    {GPIOB, GPIO_PIN_2, EXTI2_IRQn},
    {GPIOB, GPIO_PIN_4, EXTI4_IRQn},
    {GPIOB, GPIO_PIN_10, EXTI15_10_IRQn},
    {GPIOB, GPIO_PIN_11, EXTI15_10_IRQn},
    {GPIOB, GPIO_PIN_5, EXTI9_5_IRQn},
};

constexpr std::uint32_t buttonIrqPriority = 5u;

bool configureFrontend(void* context) {
    const auto& input = *static_cast<FrontendInput*>(context);
    // The frontend provides the pull-up and RC debounce; the falling edge is
    // the press.
    GPIO_InitTypeDef gpio{};
    gpio.Pin = input.pin;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(input.port, &gpio);
    HAL_NVIC_SetPriority(input.irq, buttonIrqPriority, 0u);
    HAL_NVIC_EnableIRQ(input.irq);
    return true;
}

bool readFrontend(void* context) {
    const auto& input = *static_cast<FrontendInput*>(context);
    return HAL_GPIO_ReadPin(input.port, input.pin) == GPIO_PIN_SET;
}

// --- Reset cause -----------------------------------------------------------

const char* capturedResetCause = "unknown";

const char* readResetCause() {
    const auto flags = RCC->CSR;
    // Every internal reset also pulses NRST, so the pin flag is checked last.
    if (flags & RCC_CSR_LPWRRSTF) return "low-power";
    if (flags & RCC_CSR_WWDGRSTF) return "window watchdog";
    if (flags & RCC_CSR_IWDGRSTF) return "independent watchdog";
    if (flags & RCC_CSR_SFTRSTF) return "software";
    if (flags & RCC_CSR_BORRSTF) return "power-on/brown-out";
    if (flags & RCC_CSR_OBLRSTF) return "option-byte load";
    if (flags & RCC_CSR_FWRSTF) return "firewall";
    if (flags & RCC_CSR_PINRSTF) return "reset pin";
    return "unknown";
}

// --- Fatal state -----------------------------------------------------------

void fatalWrite(const char* text) {
    if (!vcpReady(nullptr)) return;
    for (; *text; ++text) {
        // Bounded wait so a misconfigured UART cannot hang the fatal path.
        for (std::uint32_t spins = 0u; (USART1->ISR & USART_ISR_TXE) == 0u; ++spins) {
            if (spins > 100000u) return;
        }
        USART1->TDR = static_cast<std::uint8_t>(*text);
    }
}

} // namespace

void init() {
    enableCycleCounter();
    capturedResetCause = readResetCause();
    RCC->CSR |= RCC_CSR_RMVF;
}

slstm32::Runtime runtime() {
    return {&millis, &enterCritical, &exitCritical};
}

void delayMicroseconds(std::uint32_t microseconds) {
    const auto startedAt = DWT->CYCCNT;
    const auto cycles = microseconds * (SystemCoreClock / 1000000u);
    while (DWT->CYCCNT - startedAt < cycles) {
    }
}

slstm32::drivers::SerialOutputHardware vcpOutput() {
    return {nullptr, &vcpReady, &vcpWrite};
}

slstm32::drivers::ButtonConfig buttonFrontend(ButtonFrontend frontend, std::uint8_t id) {
    auto& input = frontendInputs[static_cast<std::size_t>(frontend)];
    return {
        id,
        input.pin,
        {&input, &configureFrontend, &readFrontend, false},
        slstm32::drivers::ButtonInterrupt::pressed,
    };
}

const char* resetCause() {
    return capturedResetCause;
}

void uniqueId(std::uint8_t (&out)[12]) {
    const auto* uid = reinterpret_cast<const std::uint8_t*>(UID_BASE);
    for (std::size_t index = 0u; index < sizeof out; ++index) out[index] = uid[index];
}

void logBoardInfo(const slstm32::Logger& log) {
    using slstm32::LogLevel;
    const auto* uid = reinterpret_cast<const std::uint32_t*>(UID_BASE);
    const auto flashKiB = *reinterpret_cast<const std::uint16_t*>(FLASHSIZE_BASE);
    log.writef(LogLevel::info, "reset cause: %s", resetCause());
    log.writef(LogLevel::info, "clock: SYSCLK %lu Hz, HSE %s, LSE %s",
               static_cast<unsigned long>(HAL_RCC_GetSysClockFreq()),
               (RCC->CR & RCC_CR_HSERDY) ? "ok" : "off",
               (RCC->BDCR & RCC_BDCR_LSERDY) ? "ok" : "off");
    log.writef(LogLevel::info, "mcu: id 0x%03lx rev 0x%04lx, flash %u KiB, uid %08lx%08lx%08lx",
               static_cast<unsigned long>(DBGMCU->IDCODE & DBGMCU_IDCODE_DEV_ID),
               static_cast<unsigned long>(DBGMCU->IDCODE >> DBGMCU_IDCODE_REV_ID_Pos),
               static_cast<unsigned>(flashKiB), static_cast<unsigned long>(uid[2]),
               static_cast<unsigned long>(uid[1]), static_cast<unsigned long>(uid[0]));
}

} // namespace can_mcu

extern "C" void can_mcu_fatal_error(const char* reason) {
    __disable_irq();
    can_mcu::fatalWrite("\r\n*** FATAL: ");
    can_mcu::fatalWrite(reason ? reason : "unspecified");
    can_mcu::fatalWrite("\r\n");

    // Yellow CAN LED on PB0, active low.
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio{};
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
    can_mcu::enableCycleCounter();
    for (;;) {
        GPIOB->ODR ^= GPIO_PIN_0;
        can_mcu::delayMicroseconds(100000u);
    }
}
