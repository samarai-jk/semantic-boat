#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"

namespace slstm32::drivers {

// Sensirion SCD40/SCD41 photoacoustic CO2 sensor (I2C 0x62), with its
// temperature and humidity outputs. Periodic measurement (one result every
// 5 s): the driver polls "data ready" and reads CO2 (ppm), temperature, and
// humidity, each word CRC-checked.
//
// Startup: stop a measurement that may still be running (500 ms, not
// blocking), read the serial number and, where the firmware has the
// command, the variant (SCD40 / SCD41 / SCD43), start periodic measurement.
// setAmbientPressure() passes the barometric pressure to the sensor's
// compensation (at sea level about 1.3 % per 10 hPa otherwise); call it
// regularly, e.g. every minute. Automatic self-calibration stays enabled
// (sensor default): it assumes fresh air (about 400 ppm) at least once a
// week. Short waits for command results (1 ms) busy-wait.
class Scd4xDriver final : public Driver {
public:
    static constexpr std::uint8_t defaultAddress = 0x62u;

    Scd4xDriver(Runtime runtime, I2cBus& bus, Logger log, sensors::Measurement* co2,
                sensors::Measurement* temperature = nullptr, sensors::Measurement* humidity = nullptr,
                std::uint8_t busIndex = 0u, std::uint8_t address = defaultAddress)
        : runtime_(runtime), bus_(bus), log_(log), co2_(co2), temperature_(temperature), humidity_(humidity),
          busIndex_(busIndex), address_(address) {}

    bool init() override;
    void run() override;
    // Ambient pressure in hPa for the CO2 compensation (700..1200).
    void setAmbientPressure(std::uint16_t hectopascal);

    bool valid() const { return valid_; }
    std::uint16_t ppm() const { return ppm_; }
    std::uint64_t serialNumber() const { return serial_; }
    std::uint32_t samples() const { return samples_; }
    std::uint32_t errors() const { return errors_; }

private:
    enum class State : std::uint8_t { stop, identify, measuring };

    bool command(std::uint16_t code);
    bool command(std::uint16_t code, std::uint16_t argument);
    bool readWords(std::uint16_t code, std::uint16_t* words, std::size_t count);
    void wait(std::uint32_t ms) const;
    void failed(sensors::Fault fault, std::uint32_t now);
    void read(std::uint32_t now);

    Runtime runtime_;
    I2cBus& bus_;
    Logger log_;
    sensors::Measurement* co2_{};
    sensors::Measurement* temperature_{};
    sensors::Measurement* humidity_{};
    std::uint8_t busIndex_{};
    std::uint8_t address_{};
    State state_{State::stop};
    std::uint32_t nextAt_{};
    std::uint64_t serial_{};
    std::uint16_t ppm_{};
    std::uint16_t pressure_{};      // pending ambient pressure, 0 = none
    std::uint32_t samples_{};
    std::uint32_t errors_{};
    bool present_{};
    bool valid_{};
};

} // namespace slstm32::drivers
