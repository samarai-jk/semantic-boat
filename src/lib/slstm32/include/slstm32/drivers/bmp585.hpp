#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"

namespace slstm32::drivers {

// Bosch BMP585 (and the register-compatible BMP581) barometric pressure
// sensor, I2C address 0x47 (SDO high) or 0x46. One forced measurement per
// period without blocking: start, poll the data-ready flag, read 24-bit
// temperature (1/65536 C) and pressure (1/64 Pa).
//
// Pressure goes into a pressure channel (thousandths of hPa, i.e. 0.1 Pa),
// the die temperature into an optional temperature channel. Oversampling:
// pressure x16, temperature x2 (about 0.08 Pa RMS noise per the datasheet,
// roughly 1 cm of altitude). Startup: soft reset, chip ID (0x51 BMP585,
// 0x50 BMP581) and NVM check. A missing sensor fails the channels and is set
// up again when it answers.
//
// Managed modules: the DFRobot Gravity BMP585/BMP581 (SEN0664..SEN0667,
// I2C/UART) put a controller in front of the chip that configures it
// itself (normal mode, 1 Hz, oversampling x1) and ignores I2C writes, even
// the soft reset. The driver reads its configuration back; if it was not
// accepted, it only reads the latest result every period ("managed").
class Bmp585Driver final : public Driver {
public:
    static constexpr std::uint8_t defaultAddress = 0x47u;

    Bmp585Driver(Runtime runtime, I2cBus& bus, Logger log, sensors::Measurement* pressure,
                 sensors::Measurement* temperature = nullptr, std::uint32_t periodMs = 1000u,
                 std::uint8_t busIndex = 0u, std::uint8_t address = defaultAddress)
        : runtime_(runtime), bus_(bus), log_(log), pressure_(pressure), temperature_(temperature),
          periodMs_(periodMs), busIndex_(busIndex), address_(address) {}

    bool init() override;
    void run() override;

    bool valid() const { return valid_; }
    // Latest readings: pressure in 0.1 Pa (= thousandths of hPa), temperature in m°C.
    std::int32_t pressureDeciPascal() const { return pressure_dPa_; }
    std::int32_t milliCelsius() const { return milliCelsius_; }
    std::uint32_t conversionMs() const { return conversionMs_; }
    // The module ignored the configuration and runs the chip itself.
    bool managed() const { return managed_; }
    std::uint32_t samples() const { return samples_; }
    std::uint32_t errors() const { return errors_; }

private:
    enum class State : std::uint8_t { idle, converting };

    bool configure();
    void failed(sensors::Fault fault, std::uint32_t now);
    void finish(std::uint32_t now);

    Runtime runtime_;
    I2cBus& bus_;
    Logger log_;
    sensors::Measurement* pressure_{};
    sensors::Measurement* temperature_{};
    std::uint32_t periodMs_{};
    std::uint8_t busIndex_{};
    std::uint8_t address_{};
    State state_{State::idle};
    std::uint32_t nextAt_{};
    std::uint32_t cycleAt_{};
    std::uint32_t startedAt_{};
    std::uint32_t conversionMs_{};
    std::int32_t pressure_dPa_{};
    std::int32_t milliCelsius_{};
    std::uint32_t samples_{};
    std::uint32_t errors_{};
    bool configured_{};
    bool managed_{};
    bool valid_{};
};

} // namespace slstm32::drivers
