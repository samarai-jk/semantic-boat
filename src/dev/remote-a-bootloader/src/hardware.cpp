#include "hardware.hpp"
#include "stm32l4xx.h"

namespace boot::hw {

namespace {

volatile std::uint32_t ticks;
// Clock enables as after reset (some are on then, e.g. the RTC's bus
// clock), restored before the application starts.
std::uint32_t resetAhb2Enr;
std::uint32_t resetApb1Enr1;
std::uint32_t resetApb2Enr;

constexpr std::uint8_t eepromAddress = 0x52u;
constexpr std::uint32_t eepromTimeoutMs = 10u;

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

// Waits for an I2C2 flag; false on NACK, bus error, or timeout.
bool i2cWait(std::uint32_t flag) {
    const auto start = ticks;
    while ((I2C2->ISR & flag) == 0u) {
        if ((I2C2->ISR & (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO)) != 0u || ticks - start > eepromTimeoutMs) {
            return false;
        }
    }
    return true;
}

void i2cAbort() {
    I2C2->CR2 |= I2C_CR2_STOP;
    const auto start = ticks;
    while ((I2C2->ISR & I2C_ISR_BUSY) != 0u && ticks - start <= eepromTimeoutMs) {
    }
    // A software reset clears the state machine and all flags.
    I2C2->CR1 &= ~I2C_CR1_PE;
    I2C2->CR1 |= I2C_CR1_PE;
}

} // namespace

std::uint32_t startClock() {
    resetAhb2Enr = RCC->AHB2ENR;
    resetApb1Enr1 = RCC->APB1ENR1;
    resetApb2Enr = RCC->APB2ENR;
    RCC->CR |= RCC_CR_HSEON;
    if (waitFor(RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY, 200000u)) {
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

void startLog(std::uint32_t systemClockHz) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    setAlternate(GPIOA, 9u, 7u); // USART1_TX
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

void startRs485(std::uint32_t systemClockHz) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    setAlternate(GPIOA, 1u, 7u); // USART2_DE
    setAlternate(GPIOA, 2u, 7u); // USART2_TX
    setAlternate(GPIOA, 3u, 7u); // USART2_RX
    USART2->CR1 = 0u;
    USART2->BRR = (systemClockHz + 57600u) / 115200u;
    USART2->CR3 = USART_CR3_DEM; // driver enable by the USART, active high
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

bool rs485Read(std::uint8_t& byte) {
    const auto status = USART2->ISR;
    if (status & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        USART2->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    }
    if ((status & USART_ISR_RXNE) == 0u) return false;
    byte = static_cast<std::uint8_t>(USART2->RDR);
    return true;
}

void rs485Write(const std::uint8_t* data, std::size_t size) {
    // The transceiver also receives what it sends. Polled, that echo would
    // overrun the receiver and leave a partial frame in front of the next
    // request: the receiver is off while sending.
    USART2->CR1 &= ~USART_CR1_RE;
    for (std::size_t index = 0u; index < size; ++index) {
        while (!(USART2->ISR & USART_ISR_TXE)) {
        }
        USART2->TDR = data[index];
    }
    while (!(USART2->ISR & USART_ISR_TC)) {
    }
    (void)USART2->RDR;
    USART2->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    USART2->CR1 |= USART_CR1_RE;
}

void startLed() {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    GPIOB->BSRR = GPIO_BSRR_BR12;
    setPinMode(GPIOB, 12u, 1u);
}

void setLed(bool on) { GPIOB->BSRR = on ? GPIO_BSRR_BS12 : GPIO_BSRR_BR12; }

void startEeprom(std::uint32_t systemClockHz) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_I2C2EN;
    constexpr unsigned pins[]{13u, 14u};
    for (const unsigned pin : pins) {
        GPIOB->OTYPER |= 1u << pin; // open drain
        GPIOB->PUPDR = (GPIOB->PUPDR & ~(3u << (2u * pin))) | (1u << (2u * pin));
        setAlternate(GPIOB, pin, 4u); // I2C2_SCL, I2C2_SDA
    }
    I2C2->CR1 = 0u;
    // 100 kHz from an 8 or 16 MHz I2C clock (RM0394 timing examples).
    I2C2->TIMINGR = systemClockHz == 8000000u ? 0x10420F13u : 0x30420F13u;
    I2C2->CR1 = I2C_CR1_PE;
}

bool eepromRead(std::uint16_t address, std::uint8_t* data, std::size_t size) {
    if (size == 0u || size > 255u) return false;
    I2C2->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF;
    // Memory address (two bytes), then a repeated start for the read.
    I2C2->CR2 = (eepromAddress << 1u) | (2u << I2C_CR2_NBYTES_Pos) | I2C_CR2_START;
    if (!i2cWait(I2C_ISR_TXIS)) { i2cAbort(); return false; }
    I2C2->TXDR = static_cast<std::uint8_t>(address >> 8u);
    if (!i2cWait(I2C_ISR_TXIS)) { i2cAbort(); return false; }
    I2C2->TXDR = static_cast<std::uint8_t>(address);
    if (!i2cWait(I2C_ISR_TC)) { i2cAbort(); return false; }
    I2C2->CR2 = (eepromAddress << 1u) | I2C_CR2_RD_WRN | (size << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND |
                I2C_CR2_START;
    for (std::size_t index = 0u; index < size; ++index) {
        if (!i2cWait(I2C_ISR_RXNE)) { i2cAbort(); return false; }
        data[index] = static_cast<std::uint8_t>(I2C2->RXDR);
    }
    if (!i2cWait(I2C_ISR_STOPF)) { i2cAbort(); return false; }
    I2C2->ICR = I2C_ICR_STOPCF;
    return true;
}

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

void startApplication(std::uint32_t base) {
    __disable_irq();
    SysTick->CTRL = 0u;
    SysTick->VAL = 0u;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    // Peripherals back to reset, their clocks off.
    RCC->APB1RSTR1 |= RCC_APB1RSTR1_USART2RST | RCC_APB1RSTR1_I2C2RST;
    RCC->APB1RSTR1 &= ~(RCC_APB1RSTR1_USART2RST | RCC_APB1RSTR1_I2C2RST);
    RCC->APB2RSTR |= RCC_APB2RSTR_USART1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_USART1RST;
    RCC->AHB2RSTR |= RCC_AHB2RSTR_GPIOARST | RCC_AHB2RSTR_GPIOBRST;
    RCC->AHB2RSTR &= ~(RCC_AHB2RSTR_GPIOARST | RCC_AHB2RSTR_GPIOBRST);
    RCC->APB1ENR1 = resetApb1Enr1;
    RCC->APB2ENR = resetApb2Enr;
    RCC->AHB2ENR = resetAhb2Enr;
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
