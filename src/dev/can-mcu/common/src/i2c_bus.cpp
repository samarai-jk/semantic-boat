#include "can_mcu/i2c_bus.hpp"
#include "can_mcu/board.hpp"
extern "C" {
#include "main.h"
extern I2C_HandleTypeDef hi2c1;
}

namespace can_mcu {
namespace {

// Long enough for a device that stretches the clock (BNO055).
constexpr std::uint32_t probeTimeoutMs = 10u;
constexpr std::uint32_t transferTimeoutMs = 25u;
constexpr std::uint32_t recoveryHalfPeriodUs = 5u;
constexpr std::uint16_t sclPin = GPIO_PIN_8;
constexpr std::uint16_t sdaPin = GPIO_PIN_9;

std::uint16_t deviceAddress(std::uint8_t address) {
    return static_cast<std::uint16_t>(address << 1u);
}

void configureOpenDrain(std::uint16_t pins) {
    GPIO_InitTypeDef gpio{};
    gpio.Pin = pins;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
}

void setPin(std::uint16_t pin, bool high) {
    HAL_GPIO_WritePin(GPIOB, pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool sdaHigh() {
    return HAL_GPIO_ReadPin(GPIOB, sdaPin) == GPIO_PIN_SET;
}

} // namespace

bool HalI2cBus::finish(int status) {
    if (status == HAL_OK) return true;
    ++errors_;
    // A NACK is a normal answer; everything else may have left the bus stuck.
    const bool nackOnly = status == HAL_ERROR && hi2c1.ErrorCode == HAL_I2C_ERROR_AF;
    if (!nackOnly) recover();
    return false;
}

bool HalI2cBus::probe(std::uint8_t address) {
    // A one-byte read, like i2cdetect's read probe. An empty write (address
    // and stop only, HAL_I2C_IsDeviceReady) confuses some devices: the BNO055
    // then took its next register access wrongly and stopped its fusion with
    // a configuration error after every bus scan.
    std::uint8_t byte = 0u;
    const auto status = HAL_I2C_Master_Receive(&hi2c1, deviceAddress(address), &byte, 1u, probeTimeoutMs);
    if (status == HAL_OK) return true;
    // Absent devices are expected during scans and write-cycle polling: the
    // HAL reports them as HAL_ERROR with an acknowledge failure, so only a
    // busy bus or a bus/arbitration error is a fault.
    constexpr std::uint32_t busFaults = HAL_I2C_ERROR_BERR | HAL_I2C_ERROR_ARLO | HAL_I2C_ERROR_OVR;
    if (status != HAL_ERROR || (hi2c1.ErrorCode & busFaults) != 0u) {
        ++errors_;
        recover();
    }
    return false;
}

bool HalI2cBus::write(std::uint8_t address, const std::uint8_t* data, std::size_t size) {
    return finish(HAL_I2C_Master_Transmit(&hi2c1, deviceAddress(address),
                                          const_cast<std::uint8_t*>(data),
                                          static_cast<std::uint16_t>(size), transferTimeoutMs));
}

bool HalI2cBus::read(std::uint8_t address, std::uint8_t* data, std::size_t size) {
    return finish(HAL_I2C_Master_Receive(&hi2c1, deviceAddress(address), data,
                                         static_cast<std::uint16_t>(size), transferTimeoutMs));
}

bool HalI2cBus::readFrom(std::uint8_t address, const std::uint8_t* prefix, std::size_t prefixSize,
                         std::uint8_t* data, std::size_t size) {
    if (prefixSize == 1u || prefixSize == 2u) {
        // Write prefix, repeated start, read.
        const auto memoryAddress = prefixSize == 1u
            ? static_cast<std::uint16_t>(prefix[0])
            : static_cast<std::uint16_t>(prefix[0] << 8u | prefix[1]);
        return finish(HAL_I2C_Mem_Read(&hi2c1, deviceAddress(address), memoryAddress,
                                       prefixSize == 1u ? I2C_MEMADD_SIZE_8BIT : I2C_MEMADD_SIZE_16BIT,
                                       data, static_cast<std::uint16_t>(size), transferTimeoutMs));
    }
    // Longer prefixes: write with STOP, then read.
    return write(address, prefix, prefixSize) && read(address, data, size);
}

void HalI2cBus::recover() {
    ++recoveries_;
    HAL_I2C_DeInit(&hi2c1);

    configureOpenDrain(sclPin | sdaPin);
    setPin(sdaPin, true);
    setPin(sclPin, true);
    delayMicroseconds(recoveryHalfPeriodUs);
    // Clock out a slave that is holding SDA low in the middle of a byte.
    for (int pulse = 0; pulse < 9 && !sdaHigh(); ++pulse) {
        setPin(sclPin, false);
        delayMicroseconds(recoveryHalfPeriodUs);
        setPin(sclPin, true);
        delayMicroseconds(recoveryHalfPeriodUs);
    }
    // STOP condition: SDA rises while SCL is high.
    setPin(sclPin, false);
    delayMicroseconds(recoveryHalfPeriodUs);
    setPin(sdaPin, false);
    delayMicroseconds(recoveryHalfPeriodUs);
    setPin(sclPin, true);
    delayMicroseconds(recoveryHalfPeriodUs);
    setPin(sdaPin, true);
    delayMicroseconds(recoveryHalfPeriodUs);

    // MSP init restores the alternate function; filters as set up by CubeMX.
    HAL_I2C_Init(&hi2c1);
    HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE);
    HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0u);
}

HalI2cBus& i2cBus() {
    static HalI2cBus bus;
    return bus;
}

} // namespace can_mcu
