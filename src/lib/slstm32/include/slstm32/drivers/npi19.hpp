#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"

namespace slstm32::drivers {

// Amphenol NovaSensor NPI-19 digital pressure sensor with I2C output
// (ordering code NPI-19x-yyyG2/A2, address 0x28). One measurement per period:
// full measurement command 0xAC, then read four bytes without blocking until
// the data are fresh.
//
// The datasheet describes a status byte followed by 16-bit bridge and
// temperature words; the sensor on the bench (NPI-19J-030G2) answers in the
// ZSSC3123 / Honeywell format instead: 2 status bits (0 fresh, 1 command mode,
// 2 stale, 3 diagnostic) and 14 bits bridge, then 11 bits temperature over
// -50..150 °C. The bridge reads 1638 counts at the lower and 14746 at the
// upper end of the range given in the constructor (in 0.1 Pa, the pressure
// quantity's unit; 0..2070000 for the 30 psi gauge sensor). Both channels'
// raw values are the sensor counts.
//
// A missing sensor fails the channels with noResponse and is retried every
// period.
class Npi19Driver final : public Driver {
public:
    static constexpr std::uint8_t defaultAddress = 0x28u;

    Npi19Driver(Runtime runtime, I2cBus& bus, Logger log, sensors::Measurement* pressure,
                sensors::Measurement* temperature, std::int32_t fullScale, std::int32_t zeroScale = 0,
                std::uint32_t periodMs = 1000u, std::uint8_t busIndex = 0u, std::uint8_t address = defaultAddress)
        : runtime_(runtime), bus_(bus), log_(log), pressure_(pressure), temperature_(temperature),
          zeroScale_(zeroScale), fullScale_(fullScale), periodMs_(periodMs), busIndex_(busIndex),
          address_(address) {}

    bool init() override;
    void run() override;

    bool valid() const { return valid_; }
    std::int32_t pressure() const { return pressureValue_; } // 0.1 Pa
    std::int32_t milliCelsius() const { return milliCelsius_; }
    std::uint8_t status() const { return status_; } // 0..3, see above
    std::uint16_t bridgeCounts() const { return bridgeCounts_; }
    std::uint16_t temperatureCounts() const { return temperatureCounts_; }
    std::uint32_t samples() const { return samples_; }
    std::uint32_t errors() const { return errors_; }

private:
    enum class State : std::uint8_t { idle, converting };

    void finish(std::uint32_t now);
    void failed(sensors::Fault fault, std::uint32_t now);

    Runtime runtime_;
    I2cBus& bus_;
    Logger log_;
    sensors::Measurement* pressure_{};
    sensors::Measurement* temperature_{};
    std::int32_t zeroScale_{};
    std::int32_t fullScale_{};
    std::uint32_t periodMs_{};
    std::uint8_t busIndex_{};
    std::uint8_t address_{};
    State state_{State::idle};
    std::uint32_t nextAt_{};
    std::uint32_t cycleAt_{};
    std::uint8_t busyRetries_{};
    std::uint8_t status_{};
    std::uint16_t bridgeCounts_{};
    std::uint16_t temperatureCounts_{};
    std::int32_t pressureValue_{};
    std::int32_t milliCelsius_{};
    std::uint32_t samples_{};
    std::uint32_t errors_{};
    std::uint32_t rangeWarnedAt_{};
    bool valid_{};
    bool present_{};
    bool rangeWarned_{};
};

} // namespace slstm32::drivers
