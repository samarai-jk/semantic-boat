#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"

namespace slstm32::drivers {

// DFRobot SEN0636 UV index sensor (240..370 nm, UVA/UVB/UVC), I2C address
// 0x23 with the module's mode switch on I2C. The module has its own
// controller with 16-bit registers (little-endian on I2C, as in DFRobot's
// library): 0x00 product ID 0x427C, 0x05 firmware version, 0x06 raw UV
// value, 0x07 UV index (integer), 0x08 risk level.
//
// Reads every period into a UV index channel; the risk level follows the
// WHO categories (0 low, 1 moderate, 2 high, 3 very high, 4 extreme). A
// missing module fails the channel with noResponse and is checked again
// (product ID) when it answers.
class DfrobotUvIndexDriver final : public Driver {
public:
    static constexpr std::uint8_t defaultAddress = 0x23u;
    static constexpr std::uint16_t productId = 0x427Cu;

    DfrobotUvIndexDriver(Runtime runtime, I2cBus& bus, Logger log, sensors::Measurement* uvIndex,
                         std::uint32_t periodMs = 1000u, std::uint8_t busIndex = 0u,
                         std::uint8_t address = defaultAddress)
        : runtime_(runtime), bus_(bus), log_(log), uvIndex_(uvIndex), periodMs_(periodMs), busIndex_(busIndex),
          address_(address) {}

    bool init() override;
    void run() override;

    bool valid() const { return valid_; }
    std::uint16_t index() const { return index_; }
    std::uint16_t riskLevel() const { return riskLevel_; }
    std::uint16_t raw() const { return raw_; }
    static const char* riskName(std::uint16_t level);

private:
    bool readRegisters(std::uint8_t first, std::uint16_t* values, std::uint8_t count);
    void failed(sensors::Fault fault, std::uint32_t now);

    Runtime runtime_;
    I2cBus& bus_;
    Logger log_;
    sensors::Measurement* uvIndex_{};
    std::uint32_t periodMs_{};
    std::uint8_t busIndex_{};
    std::uint8_t address_{};
    std::uint32_t nextAt_{};
    std::uint16_t index_{};
    std::uint16_t riskLevel_{};
    std::uint16_t raw_{};
    bool present_{};
    bool valid_{};
};

} // namespace slstm32::drivers
