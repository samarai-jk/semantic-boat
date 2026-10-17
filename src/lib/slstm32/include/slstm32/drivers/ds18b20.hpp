#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/one_wire.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"

namespace slstm32::drivers {

// Maxim DS18B20 (and DS1822) 1-Wire temperature sensor, externally powered
// (three wires; parasite power is not supported), one sensor per bus. The
// driver reads the ROM code of the sensor on the bus, uses it as the
// channel's identity (so calibration follows the sensor, not the input),
// starts a conversion each period, and reads the scratchpad after the
// conversion time of the configured resolution (750 ms at 12 bit) without
// blocking.
//
// A missing sensor fails the channel with noResponse and is looked for
// again every period; a different sensor on the same input is picked up
// with its own identity and settings. The 85 C power-on value of the first
// conversion is discarded.
class Ds18b20Driver final : public Driver {
public:
    Ds18b20Driver(Runtime runtime, OneWireBus& bus, Logger log, sensors::Measurement* temperature,
                  std::uint32_t periodMs = 2000u)
        : runtime_(runtime), bus_(bus), log_(log), temperature_(temperature), periodMs_(periodMs) {}

    bool init() override;
    void run() override;

    OneWireBus::Rom rom() const { return rom_; }
    bool valid() const { return valid_; }
    std::int32_t milliCelsius() const { return milliCelsius_; }
    std::uint8_t resolutionBits() const { return resolutionBits_; }
    std::uint32_t samples() const { return samples_; }
    std::uint32_t errors() const { return errors_; }

private:
    enum class State : std::uint8_t { idle, converting };

    bool discover(std::uint32_t now);
    bool readScratchpad(std::uint8_t (&data)[9]);
    void finish(std::uint32_t now);
    void failed(sensors::Fault fault, std::uint32_t now);

    Runtime runtime_;
    OneWireBus& bus_;
    Logger log_;
    sensors::Measurement* temperature_{};
    std::uint32_t periodMs_{};
    State state_{State::idle};
    OneWireBus::Rom rom_{};
    OneWireBus::Rom foreignRom_{}; // last other device reported, to log it once
    std::uint32_t nextAt_{};
    std::uint32_t cycleAt_{};
    std::uint32_t conversionMs_{750u};
    std::int32_t milliCelsius_{};
    std::uint32_t samples_{};
    std::uint32_t errors_{};
    std::uint8_t resolutionBits_{12u};
    bool valid_{};
    bool firstConversion_{};
    bool lineWarned_{};
};

} // namespace slstm32::drivers
