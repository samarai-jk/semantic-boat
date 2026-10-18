#include "hardware.hpp"
#include "stm32l4xx.h"

namespace boot::hw {

namespace {

volatile std::uint32_t ticks;
bool usingHse;

constexpr std::uint32_t flashErrors = FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR |
                                      FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR |
                                      FLASH_SR_RDERR | FLASH_SR_OPTVERR;

bool waitFor(volatile std::uint32_t& reg, std::uint32_t mask, std::uint32_t value, std::uint32_t loops) {
    while ((reg & mask) != value) {
        if (loops-- == 0u) return false;
    }
    return true;
}

void setPinMode(GPIO_TypeDef* port, unsigned pin, std::uint32_t mode) {
    port->MODER = (port->MODER & ~(3u << (2u * pin))) | (mode << (2u * pin));
}

void setAlternate(GPIO_TypeDef* port, unsigned pin, std::uint32_t function) {
    auto& afr = port->AFR[pin / 8u];
    const auto shift = 4u * (pin % 8u);
    afr = (afr & ~(0xFu << shift)) | (function << shift);
    port->OSPEEDR |= 3u << (2u * pin);
    setPinMode(port, pin, 2u);
}

bool flashWait() {
    while (FLASH->SR & FLASH_SR_BSY) {
    }
    const bool ok = (FLASH->SR & flashErrors) == 0u;
    FLASH->SR = flashErrors | FLASH_SR_EOP; // write 1 to clear
    return ok;
}

void flashUnlock() {
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123u;
        FLASH->KEYR = 0xCDEF89ABu;
    }
}

} // namespace

std::uint32_t startClock() {
    RCC->CR |= RCC_CR_HSEON;
    usingHse = waitFor(RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY, 200000u);
    if (usingHse) {
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSE;
        waitFor(RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSE, 200000u);
        return 8000000u;
    }
    RCC->CR &= ~RCC_CR_HSEON;
    RCC->CR |= RCC_CR_HSION;
    waitFor(RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY, 200000u);
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
    waitFor(RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI, 200000u);
    return 16000000u;
}

void startTick(std::uint32_t systemClockHz) {
    SysTick->LOAD = systemClockHz / 1000u - 1u;
    SysTick->VAL = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

std::uint32_t millis() { return ticks; }

void delay(std::uint32_t ms) {
    const auto start = ticks;
    while (ticks - start < ms) {
    }
}

void startLog(std::uint32_t systemClockHz) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    setAlternate(GPIOB, 6u, 7u); // USART1_TX
    USART1->BRR = (systemClockHz + 57600u) / 115200u;
    USART1->CR1 = USART_CR1_TE | USART_CR1_UE;
}

void log(const char* text) {
    const auto put = [](char character) {
        while (!(USART1->ISR & USART_ISR_TXE)) {
        }
        USART1->TDR = static_cast<std::uint8_t>(character);
    };
    for (const char* cursor = text; *cursor != '\0'; ++cursor) put(*cursor);
    put('\r');
    put('\n');
}

void logHex(const char* text, std::uint32_t value) {
    char line[64];
    std::size_t length = 0u;
    while (text[length] != '\0' && length < sizeof line - 12u) {
        line[length] = text[length];
        ++length;
    }
    line[length++] = ' ';
    for (int shift = 28; shift >= 0; shift -= 4) line[length++] = "0123456789ABCDEF"[(value >> shift) & 0xFu];
    line[length] = '\0';
    log(line);
}

bool startCan(std::uint32_t systemClockHz) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_CAN1EN;
    setAlternate(GPIOA, 11u, 9u); // CAN1_RX
    setAlternate(GPIOA, 12u, 9u); // CAN1_TX
    GPIOB->BSRR = GPIO_BSRR_BR1;  // transceiver high-speed mode
    setPinMode(GPIOB, 1u, 1u);
    GPIOB->BSRR = GPIO_BSRR_BS0; // LED off
    setPinMode(GPIOB, 0u, 1u);

    CAN1->MCR &= ~CAN_MCR_SLEEP;
    CAN1->MCR |= CAN_MCR_INRQ;
    if (!waitFor(CAN1->MSR, CAN_MSR_INAK, CAN_MSR_INAK, 200000u)) return false;
    CAN1->MCR |= CAN_MCR_ABOM | CAN_MCR_AWUM;
    // 250 kbit/s, 16 time quanta, sample point 87.5 % (13 + 1 before, 2 after).
    const std::uint32_t prescaler = systemClockHz / (250000u * 16u);
    CAN1->BTR = (0u << CAN_BTR_SJW_Pos) | (1u << CAN_BTR_TS2_Pos) | (12u << CAN_BTR_TS1_Pos) | (prescaler - 1u);
    // Filter bank 0: 32-bit mask 0, everything into FIFO 0.
    CAN1->FMR |= CAN_FMR_FINIT;
    CAN1->FA1R &= ~1u;
    CAN1->FS1R |= 1u;
    CAN1->FM1R &= ~1u;
    CAN1->FFA1R &= ~1u;
    CAN1->sFilterRegister[0].FR1 = 0u;
    CAN1->sFilterRegister[0].FR2 = 0u;
    CAN1->FA1R |= 1u;
    CAN1->FMR &= ~CAN_FMR_FINIT;
    CAN1->MCR &= ~CAN_MCR_INRQ;
    // Leaving initialization needs 11 recessive bits on the bus; without a
    // bus the controller joins once one appears.
    waitFor(CAN1->MSR, CAN_MSR_INAK, 0u, 200000u);
    return true;
}

bool canSend(const Frame& frame) {
    const auto start = ticks;
    std::uint32_t mailbox = 3u;
    while (mailbox == 3u) {
        const auto tsr = CAN1->TSR;
        if (tsr & CAN_TSR_TME0) mailbox = 0u;
        else if (tsr & CAN_TSR_TME1) mailbox = 1u;
        else if (tsr & CAN_TSR_TME2) mailbox = 2u;
        else if (ticks - start > 5u) return false;
    }
    auto& box = CAN1->sTxMailBox[mailbox];
    box.TIR = (frame.id << CAN_TI0R_EXID_Pos) | CAN_TI0R_IDE;
    box.TDTR = frame.length & 0xFu;
    box.TDLR = frame.data[0] | frame.data[1] << 8u | frame.data[2] << 16u | static_cast<std::uint32_t>(frame.data[3]) << 24u;
    box.TDHR = frame.data[4] | frame.data[5] << 8u | frame.data[6] << 16u | static_cast<std::uint32_t>(frame.data[7]) << 24u;
    box.TIR |= CAN_TI0R_TXRQ;
    return true;
}

bool canReceive(Frame& frame) {
    if ((CAN1->RF0R & CAN_RF0R_FMP0) == 0u) return false;
    const auto& box = CAN1->sFIFOMailBox[0];
    const auto rir = box.RIR;
    const bool extended = (rir & CAN_RI0R_IDE) != 0u;
    frame.id = extended ? rir >> CAN_RI0R_EXID_Pos : 0u;
    frame.length = static_cast<std::uint8_t>(box.RDTR & 0xFu);
    const auto low = box.RDLR;
    const auto high = box.RDHR;
    for (unsigned index = 0u; index < 4u; ++index) {
        frame.data[index] = static_cast<std::uint8_t>(low >> (8u * index));
        frame.data[4u + index] = static_cast<std::uint8_t>(high >> (8u * index));
    }
    CAN1->RF0R |= CAN_RF0R_RFOM0;
    // Standard frames and remote frames are not NMEA 2000 messages.
    return extended && (rir & CAN_RI0R_RTR) == 0u;
}

void setLed(bool on) { GPIOB->BSRR = on ? GPIO_BSRR_BR0 : GPIO_BSRR_BS0; }

bool flashErasePage(std::uint32_t address) {
    flashUnlock();
    if (!flashWait()) return false;
    const auto page = (address - FLASH_BASE) / 2048u;
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PNB) | FLASH_CR_PER | (page << FLASH_CR_PNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;
    const bool ok = flashWait();
    FLASH->CR &= ~(FLASH_CR_PER | FLASH_CR_PNB);
    FLASH->CR |= FLASH_CR_LOCK;
    return ok;
}

bool flashProgram(std::uint32_t address, const std::uint8_t* data, std::size_t size) {
    if (size % 8u != 0u || address % 8u != 0u) return false;
    flashUnlock();
    bool ok = flashWait();
    FLASH->CR |= FLASH_CR_PG;
    for (std::size_t offset = 0u; ok && offset < size; offset += 8u) {
        std::uint32_t words[2];
        for (unsigned index = 0u; index < 8u; ++index) {
            reinterpret_cast<std::uint8_t*>(words)[index] = data[offset + index];
        }
        auto* target = reinterpret_cast<volatile std::uint32_t*>(address + offset);
        target[0] = words[0];
        target[1] = words[1];
        ok = flashWait();
    }
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;
    flashFlushCaches();
    // Read back what was written.
    for (std::size_t offset = 0u; ok && offset < size; ++offset) {
        ok = *reinterpret_cast<const volatile std::uint8_t*>(address + offset) == data[offset];
    }
    return ok;
}

void flashFlushCaches() {
    FLASH->ACR &= ~(FLASH_ACR_DCEN | FLASH_ACR_ICEN);
    FLASH->ACR |= FLASH_ACR_DCRST | FLASH_ACR_ICRST;
    FLASH->ACR &= ~(FLASH_ACR_DCRST | FLASH_ACR_ICRST);
    FLASH->ACR |= FLASH_ACR_DCEN | FLASH_ACR_ICEN;
}

std::uint32_t backupRegister(unsigned index) {
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN | RCC_APB1ENR1_RTCAPBEN;
    return (&RTC->BKP0R)[index];
}

void setBackupRegister(unsigned index, std::uint32_t value) {
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN | RCC_APB1ENR1_RTCAPBEN;
    PWR->CR1 |= PWR_CR1_DBP;
    (&RTC->BKP0R)[index] = value;
}

void uniqueId(std::uint8_t (&out)[12]) {
    const auto* uid = reinterpret_cast<const std::uint8_t*>(UID_BASE);
    for (std::size_t index = 0u; index < sizeof out; ++index) out[index] = uid[index];
}

void startApplication(std::uint32_t base) {
    __disable_irq();
    SysTick->CTRL = 0u;
    SysTick->VAL = 0u;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    // Peripherals back to reset, their clocks off.
    RCC->APB1RSTR1 |= RCC_APB1RSTR1_CAN1RST;
    RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_CAN1RST;
    RCC->APB2RSTR |= RCC_APB2RSTR_USART1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_USART1RST;
    RCC->AHB2RSTR |= RCC_AHB2RSTR_GPIOARST | RCC_AHB2RSTR_GPIOBRST;
    RCC->AHB2RSTR &= ~(RCC_AHB2RSTR_GPIOARST | RCC_AHB2RSTR_GPIOBRST);
    RCC->APB1ENR1 &= ~(RCC_APB1ENR1_CAN1EN | RCC_APB1ENR1_PWREN | RCC_APB1ENR1_RTCAPBEN);
    RCC->APB2ENR &= ~RCC_APB2ENR_USART1EN;
    RCC->AHB2ENR &= ~(RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN);
    // MSI 4 MHz as after reset.
    RCC->CR |= RCC_CR_MSION;
    waitFor(RCC->CR, RCC_CR_MSIRDY, RCC_CR_MSIRDY, 200000u);
    RCC->CFGR &= ~RCC_CFGR_SW;
    waitFor(RCC->CFGR, RCC_CFGR_SWS, 0u, 200000u);
    RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_HSION);
    const auto* vectors = reinterpret_cast<const std::uint32_t*>(base);
    SCB->VTOR = base;
    __DSB();
    __ISB();
    __set_MSP(vectors[0]);
    // The application starts like after a reset: interrupts enabled, none
    // configured.
    __enable_irq();
    reinterpret_cast<void (*)()>(vectors[1])();
    while (true) {
    }
}

void reset() {
    NVIC_SystemReset();
}

} // namespace boot::hw

extern "C" void SysTick_Handler() { ++boot::hw::ticks; }
