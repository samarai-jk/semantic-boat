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
EepromIoError lastError = EepromIoError::none;

bool read(void*, std::uint16_t address, std::uint8_t* data, std::size_t size) {
    if (HAL_I2C_Mem_Read(&hi2c2, deviceAddress, address, I2C_MEMADD_SIZE_16BIT,
                         data, static_cast<std::uint16_t>(size), ioTimeoutMs) == HAL_OK) {
        return true;
    }
    lastError = EepromIoError::readFailed;
    return false;
}

bool writePage(void*, std::uint16_t address, const std::uint8_t* data, std::size_t size) {
    if (HAL_I2C_Mem_Write(&hi2c2, deviceAddress, address, I2C_MEMADD_SIZE_16BIT,
                          const_cast<std::uint8_t*>(data), static_cast<std::uint16_t>(size),
                          ioTimeoutMs) != HAL_OK) {
        lastError = EepromIoError::writeFailed;
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
