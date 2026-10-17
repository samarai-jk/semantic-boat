#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"

namespace slstm32::drivers {

// Aosong DHT20 / AHT20 temperature and humidity sensor (I2C address 0x38),
// for example the DFRobot DHT20 module. One measurement per period: trigger,
// wait 80 ms without blocking, read 6 data bytes and the CRC.
//
// Results go into the given measurement channels (either may be null); the
// driver sets their identities (I2C address, subchannel 0 = temperature,
// 1 = humidity). A missing sensor fails the channels with noResponse and is
// retried every period, so it may be plugged in at any time. A sensor that
// reports an uninitialized state gets the register initialization from the
// DHT20 datasheet (blocks about 50 ms, once per power-up).
class Dht20Driver final : public Driver {
public:
    static constexpr std::uint8_t defaultAddress = 0x38u;

    Dht20Driver(Runtime runtime, I2cBus& bus, Logger log, sensors::Measurement* temperature,
                sensors::Measurement* humidity, std::uint32_t periodMs = 2000u, std::uint8_t busIndex = 0u,
                std::uint8_t address = defaultAddress)
        : runtime_(runtime), bus_(bus), log_(log), temperature_(temperature), humidity_(humidity),
          periodMs_(periodMs), busIndex_(busIndex), address_(address) {}

    bool init() override;
    void run() override;

    bool valid() const { return valid_; }
    std::int32_t milliCelsius() const { return milliCelsius_; }
    std::int32_t milliPercent() const { return milliPercent_; }
    std::uint32_t samples() const { return samples_; }
    std::uint32_t errors() const { return errors_; }

private:
    enum class State : std::uint8_t { idle, converting };

    bool prepare();
    bool resetRegister(std::uint8_t reg);
    void wait(std::uint32_t ms) const;
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
    std::uint8_t busyRetries_{};
    std::int32_t milliCelsius_{};
    std::int32_t milliPercent_{};
    std::uint32_t samples_{};
    std::uint32_t errors_{};
    bool valid_{};
    bool present_{};
};

} // namespace slstm32::drivers
