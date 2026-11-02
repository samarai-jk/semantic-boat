#include "hal_eeprom.hpp"
#include <cstring>
extern "C" {
#include "main.h"
extern I2C_HandleTypeDef hi2c2;
}

namespace remote_a {
namespace {

// U4 is an AT24C256C. Remote-A's schematic straps its address pins for 0x52
// and explicitly annotates the EEPROM block "ADR 0x52".
constexpr std::uint16_t deviceAddress = eepromI2cAddress << 1u;
constexpr std::uint32_t ioTimeoutMs = 250u;
constexpr std::uint32_t writeCycleTimeoutMs = 10u;
// The HAL takes the timeout's start tick once per call, so it limits the whole
// transfer: at 100 kHz a byte takes 90 us, and one 250 ms read ended after
// about 2.7 KiB. A larger configuration package then timed out on every boot
// and left the peripheral holding the bus. Each chunk (about 23 ms) gets its
// own timeout.
constexpr std::size_t readChunkBytes = 256u;
EepromIoError lastError = EepromIoError::none;

void delayMicroseconds(std::uint32_t microseconds) {
    const auto cycles = SystemCoreClock / 1000000u * microseconds / 4u;
    for (volatile std::uint32_t count = 0u; count < cycles; ++count) {
    }
}

// After a failed transfer the peripheral may still stretch SCL, or the EEPROM
// may hold SDA low in the middle of a byte; either blocks every later access
// until power is removed. Reset the peripheral, clock out a stuck slave (up to
// nine SCL pulses, then a STOP), and initialize the peripheral as CubeMX does.
void recoverBus() {
    HAL_I2C_DeInit(&hi2c2);
    GPIO_InitTypeDef gpio{};
    gpio.Pin = MCU_I2C_SCL_Pin | MCU_I2C_SDA_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_WritePin(MCU_I2C_SDA_GPIO_Port, MCU_I2C_SDA_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MCU_I2C_SCL_GPIO_Port, MCU_I2C_SCL_Pin, GPIO_PIN_SET);
    HAL_GPIO_Init(MCU_I2C_SCL_GPIO_Port, &gpio);
    for (unsigned pulse = 0u; pulse < 9u &&
         HAL_GPIO_ReadPin(MCU_I2C_SDA_GPIO_Port, MCU_I2C_SDA_Pin) == GPIO_PIN_RESET; ++pulse) {
        HAL_GPIO_WritePin(MCU_I2C_SCL_GPIO_Port, MCU_I2C_SCL_Pin, GPIO_PIN_RESET);
        delayMicroseconds(5u);
        HAL_GPIO_WritePin(MCU_I2C_SCL_GPIO_Port, MCU_I2C_SCL_Pin, GPIO_PIN_SET);
        delayMicroseconds(5u);
    }
    HAL_GPIO_WritePin(MCU_I2C_SDA_GPIO_Port, MCU_I2C_SDA_Pin, GPIO_PIN_RESET);
    delayMicroseconds(5u);
    HAL_GPIO_WritePin(MCU_I2C_SDA_GPIO_Port, MCU_I2C_SDA_Pin, GPIO_PIN_SET);
    delayMicroseconds(5u);
    HAL_GPIO_DeInit(MCU_I2C_SCL_GPIO_Port, MCU_I2C_SCL_Pin | MCU_I2C_SDA_Pin);
    if (HAL_I2C_Init(&hi2c2) == HAL_OK) {
        HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE);
        HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0u);
    }
}

bool read(void*, std::uint16_t address, std::uint8_t* data, std::size_t size) {
    while (size != 0u) {
        const auto amount = size < readChunkBytes ? size : readChunkBytes;
        if (HAL_I2C_Mem_Read(&hi2c2, deviceAddress, address, I2C_MEMADD_SIZE_16BIT,
                             data, static_cast<std::uint16_t>(amount), ioTimeoutMs) != HAL_OK) {
            lastError = EepromIoError::readFailed;
            recoverBus();
            return false;
        }
        address = static_cast<std::uint16_t>(address + amount);
        data += amount;
        size -= amount;
    }
    return true;
}

bool writePage(void*, std::uint16_t address, const std::uint8_t* data, std::size_t size) {
    if (HAL_I2C_Mem_Write(&hi2c2, deviceAddress, address, I2C_MEMADD_SIZE_16BIT,
                          const_cast<std::uint8_t*>(data), static_cast<std::uint16_t>(size),
                          ioTimeoutMs) != HAL_OK) {
        lastError = EepromIoError::writeFailed;
        recoverBus();
        return false;
    }

    // An AT24C256C does not acknowledge its address during the internal page-write
    // cycle. Counting probes is not a useful timeout: twenty fast NACKs can occur
    // well inside the part's 5 ms maximum write time. Poll against elapsed time.
    const auto startedAt = HAL_GetTick();
    bool ready = false;
    do {
        if (HAL_I2C_IsDeviceReady(&hi2c2, deviceAddress, 1u, 2u) == HAL_OK) {
            ready = true;
            break;
        }
        HAL_Delay(1u);
    } while (static_cast<std::uint32_t>(HAL_GetTick() - startedAt) < writeCycleTimeoutMs);
    if (!ready) {
        lastError = EepromIoError::writeCycleTimeout;
        return false;
    }

    // A configuration is written rarely, so favor certainty over speed and verify
    // every page before allowing the storage layer to commit it.
    std::uint8_t verification[slstm32::drivers::At24c256::pageSize]{};
    if (size > sizeof verification ||
        HAL_I2C_Mem_Read(&hi2c2, deviceAddress, address, I2C_MEMADD_SIZE_16BIT,
                         verification, static_cast<std::uint16_t>(size), ioTimeoutMs) != HAL_OK) {
        lastError = EepromIoError::verifyReadFailed;
        recoverBus();
        return false;
    }
    if (std::memcmp(data, verification, size) != 0) {
        lastError = EepromIoError::verifyMismatch;
        return false;
    }
    return true;
}

} // namespace

bool eepromReady() {
    return HAL_I2C_IsDeviceReady(&hi2c2, deviceAddress, 3u, 10u) == HAL_OK;
}

void clearEepromIoError() {
    lastError = EepromIoError::none;
}

EepromIoError eepromIoError() {
    return lastError;
}

const char* eepromIoErrorMessage() {
    switch (lastError) {
    case EepromIoError::readFailed:
        return "EEPROM READ FAILED BEFORE COMMIT.";
    case EepromIoError::writeFailed:
        return "EEPROM PAGE WRITE TRANSMIT FAILED.";
    case EepromIoError::writeCycleTimeout:
        return "EEPROM WRITE-CYCLE TIMEOUT.";
    case EepromIoError::verifyReadFailed:
        return "EEPROM WRITE VERIFY READ FAILED.";
    case EepromIoError::verifyMismatch:
        return "EEPROM WRITE VERIFY MISMATCH.";
    case EepromIoError::none:
    default:
        return "EEPROM COMMIT FAILED.";
    }
}

slstm32::drivers::At24c256Hardware eepromHardware() {
    return {nullptr, &read, &writePage};
}

} // namespace remote_a
