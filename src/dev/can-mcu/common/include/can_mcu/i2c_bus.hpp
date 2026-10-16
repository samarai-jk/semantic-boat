#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/drivers/i2c_bus.hpp"

namespace can_mcu {

// I2C1 on PB8 (SCL) / PB9 (SDA): the can-mcu's on-board EEPROM (0x52) and
// TMP75 (0x4F), and the host I2C connectors. Initialized by CubeMX.
//
// A NACK is an ordinary result (absent device). Any other failure (bus
// error, arbitration loss, timeout, busy bus) triggers a recovery: the pins
// are released, SCL is clocked until a stuck slave frees SDA, a STOP is
// generated, and the peripheral is re-initialized. This keeps the bus usable
// when sensors are plugged in or removed while running.
class HalI2cBus final : public slstm32::drivers::I2cBus {
public:
    bool probe(std::uint8_t address) override;
    bool write(std::uint8_t address, const std::uint8_t* data, std::size_t size) override;
    bool read(std::uint8_t address, std::uint8_t* data, std::size_t size) override;
    bool readFrom(std::uint8_t address, const std::uint8_t* prefix, std::size_t prefixSize,
                  std::uint8_t* data, std::size_t size) override;

    std::uint32_t errors() const { return errors_; }
    std::uint32_t recoveries() const { return recoveries_; }

private:
    bool finish(int status);
    void recover();

    std::uint32_t errors_{};
    std::uint32_t recoveries_{};
};

HalI2cBus& i2cBus();

} // namespace can_mcu
