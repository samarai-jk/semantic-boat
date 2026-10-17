#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"

namespace slstm32::drivers {

// Sensirion SHT30/SHT31/SHT35 temperature and humidity sensor (I2C address
// 0x44, or 0x45 with ADDR high), for example the DFRobot SHT31 outdoor
// sensor. One single-shot measurement per period (high repeatability, no
// clock stretching): command, 20 ms wait without blocking, then temperature
// and humidity, each with its CRC.
//
// Results go into the given measurement channels (either may be null); the
// driver sets their identities (I2C address, subchannel 0 = temperature,
// 1 = humidity). A sensor that answers for the first time gets a soft reset;
// a missing sensor fails the channels with noResponse and is retried every
// period. The built-in heater (a few degrees, to dry a condensed sensor) can
// be switched with setHeater(); readings taken with it on are not ambient.
class Sht31Driver final : public Driver {
public:
    static constexpr std::uint8_t defaultAddress = 0x44u;

    Sht31Driver(Runtime runtime, I2cBus& bus, Logger log, sensors::Measurement* temperature,
                sensors::Measurement* humidity, std::uint32_t periodMs = 2000u, std::uint8_t busIndex = 0u,
                std::uint8_t address = defaultAddress)
        : runtime_(runtime), bus_(bus), log_(log), temperature_(temperature), humidity_(humidity),
          periodMs_(periodMs), busIndex_(busIndex), address_(address) {}

    bool init() override;
    void run() override;
    bool setHeater(bool on);
    bool heater() const { return heater_; }

    bool valid() const { return valid_; }
    std::int32_t milliCelsius() const { return milliCelsius_; }
    std::int32_t milliPercent() const { return milliPercent_; }
    std::uint32_t samples() const { return samples_; }
    std::uint32_t errors() const { return errors_; }

private:
    enum class State : std::uint8_t { idle, converting };

    bool command(std::uint16_t code);
    void finish(std::uint32_t now);
    void failed(sensors::Fault fault, std::uint32_t now);

    Runtime runtime_;
    I2cBus& bus_;
    Logger log_;
    sensors::Measurement* temperature_{};
    sensors::Measurement* humidity_{};
    std::uint32_t periodMs_{};
    std::uint8_t busIndex_{};
    std::uint8_t address_{};
    State state_{State::idle};
    std::uint32_t nextAt_{};
    std::uint32_t cycleAt_{};
    std::uint8_t readRetries_{};
    std::int32_t milliCelsius_{};
    std::int32_t milliPercent_{};
    std::uint32_t samples_{};
    std::uint32_t errors_{};
    bool valid_{};
    bool present_{};
    bool heater_{};
};

} // namespace slstm32::drivers
