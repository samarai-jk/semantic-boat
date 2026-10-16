#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/runtime.hpp"

namespace slstm32::drivers {

// TI TMP75 (and register-compatible TMP175/TMP275/LM75-style) temperature
// sensor in continuous 12-bit mode (0.0625 degC, about 220 ms per conversion).
//
// A sensor that does not respond does not fail init(): it is reported through
// valid() and reconfigured automatically when it answers again, so an absent
// or unplugged sensor never stops the application.
class Tmp75Driver final : public Driver {
public:
    static constexpr std::uint8_t baseAddress = 0x48u;

    Tmp75Driver(Runtime runtime, I2cBus& bus, std::uint8_t address,
                std::uint32_t samplePeriodMs = 1000u)
        : runtime_(runtime), bus_(bus), address_(address), samplePeriodMs_(samplePeriodMs) {}

    bool init() override;
    void run() override;
    bool valid() const { return valid_; }
    std::int32_t milliCelsius() const { return milliCelsius_; }
    std::uint32_t errors() const { return errors_; }

private:
    bool configure();
    bool sample();

    Runtime runtime_;
    I2cBus& bus_;
    std::uint8_t address_{};
    std::uint32_t samplePeriodMs_{};
    std::uint32_t nextSampleAt_{};
    std::int32_t milliCelsius_{};
    std::uint32_t errors_{};
    bool configured_{};
    bool valid_{};
};

} // namespace slstm32::drivers
