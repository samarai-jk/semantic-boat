#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"

namespace slstm32::drivers {

// Vishay VEML7700 ambient light sensor (I2C address 0x10), for example the
// DFRobot SEN0228 module. Measures continuously and reads the result
// without blocking; the value goes into an illuminance channel (mlx).
//
// Gain and integration time are ranged automatically over nine steps from
// 3.6 mlx per count (gain 2, 800 ms) to 1.84 lx per count (gain 1/8,
// 25 ms), i.e. from darkness to 120 klx: a reading above 20000 counts
// selects the next less sensitive step, one below 200 counts the next more
// sensitive one. The first result after a change is discarded (the sensor
// finishes the running integration first). Readings at gain 1/4 and 1/8 get
// Vishay's non-linearity correction. Resolution 0.0036 lx/count at gain 2
// and 800 ms (datasheet revision 2022).
//
// A missing sensor fails the channel with noResponse and is configured again
// when it answers.
class Veml7700Driver final : public Driver {
public:
    static constexpr std::uint8_t defaultAddress = 0x10u;

    Veml7700Driver(Runtime runtime, I2cBus& bus, Logger log, sensors::Measurement* illuminance,
                   std::uint32_t periodMs = 1000u, std::uint8_t busIndex = 0u,
                   std::uint8_t address = defaultAddress)
        : runtime_(runtime), bus_(bus), log_(log), illuminance_(illuminance), periodMs_(periodMs),
          busIndex_(busIndex), address_(address) {}

    bool init() override;
    void run() override;

    bool valid() const { return valid_; }
    std::int32_t milliLux() const { return milliLux_; }
    std::uint16_t counts() const { return counts_; }
    // Current range step, 0 (most sensitive) .. 8.
    std::uint8_t rangeStep() const { return step_; }
    std::uint32_t samples() const { return samples_; }
    std::uint32_t errors() const { return errors_; }

private:
    bool writeRegister(std::uint8_t reg, std::uint16_t value);
    bool readRegister(std::uint8_t reg, std::uint16_t& value);
    bool configure();
    void failed(std::uint32_t now);

    Runtime runtime_;
    I2cBus& bus_;
    Logger log_;
    sensors::Measurement* illuminance_{};
    std::uint32_t periodMs_{};
    std::uint8_t busIndex_{};
    std::uint8_t address_{};
    std::uint8_t step_{4u};
    std::uint32_t nextAt_{};
    std::int32_t milliLux_{};
    std::uint16_t counts_{};
    std::uint32_t samples_{};
    std::uint32_t errors_{};
    bool configured_{};
    bool valid_{};
};

} // namespace slstm32::drivers
