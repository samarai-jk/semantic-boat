#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"

namespace slstm32::drivers {

// Bosch BMP280 barometric pressure sensor (I2C 0x76 or 0x77), also the
// pressure and temperature part of a BME280 (humidity not read). One forced
// measurement per period (pressure x16, temperature x2, about 44 ms) without
// blocking; Bosch's integer compensation with the factory trimming values.
// Pressure goes into a pressure channel (0.001 hPa = 0.1 Pa), temperature
// into an optional channel. A missing sensor fails the channels and reads
// its trimming values again when it answers.
class Bmp280Driver final : public Driver {
public:
    static constexpr std::uint8_t defaultAddress = 0x76u;

    Bmp280Driver(Runtime runtime, I2cBus& bus, Logger log, sensors::Measurement* pressure,
                 sensors::Measurement* temperature = nullptr, std::uint32_t periodMs = 1000u,
                 std::uint8_t busIndex = 0u, std::uint8_t address = defaultAddress)
        : runtime_(runtime), bus_(bus), log_(log), pressure_(pressure), temperature_(temperature),
          periodMs_(periodMs), busIndex_(busIndex), address_(address) {}

    bool init() override;
    void run() override;

    bool valid() const { return valid_; }
    std::int32_t pressureDeciPascal() const { return pressure_dPa_; }
    std::int32_t milliCelsius() const { return milliCelsius_; }

private:
    enum class State : std::uint8_t { idle, converting };
    struct Trimming {
        std::uint16_t t1{};
        std::int16_t t2{};
        std::int16_t t3{};
        std::uint16_t p1{};
        std::int16_t p2{}, p3{}, p4{}, p5{}, p6{}, p7{}, p8{}, p9{};
    };

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
    Trimming trim_{};
    State state_{State::idle};
    std::uint32_t nextAt_{};
    std::uint32_t cycleAt_{};
    std::uint32_t startedAt_{};
    std::int32_t pressure_dPa_{};
    std::int32_t milliCelsius_{};
    bool configured_{};
    bool valid_{};
};

} // namespace slstm32::drivers
