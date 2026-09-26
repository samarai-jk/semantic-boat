#include "hal_eeprom.hpp"
extern "C" {
#include "main.h"
extern I2C_HandleTypeDef hi2c2;
}

namespace remote_a {
namespace {

constexpr std::uint16_t deviceAddress = 0x50u << 1u;
constexpr std::uint32_t ioTimeoutMs = 250u;

bool read(void*, std::uint16_t address, std::uint8_t* data, std::size_t size) {
    return HAL_I2C_Mem_Read(&hi2c2, deviceAddress, address, I2C_MEMADD_SIZE_16BIT,
                            data, static_cast<std::uint16_t>(size), ioTimeoutMs) == HAL_OK;
}

bool writePage(void*, std::uint16_t address, const std::uint8_t* data, std::size_t size) {
    if (HAL_I2C_Mem_Write(&hi2c2, deviceAddress, address, I2C_MEMADD_SIZE_16BIT,
                          const_cast<std::uint8_t*>(data), static_cast<std::uint16_t>(size),
                          ioTimeoutMs) != HAL_OK) return false;
    return HAL_I2C_IsDeviceReady(&hi2c2, deviceAddress, 20u, 2u) == HAL_OK;
}

} // namespace

slstm32::drivers::At24c256Hardware eepromHardware() {
    return {nullptr, &read, &writePage};
}

} // namespace remote_a
