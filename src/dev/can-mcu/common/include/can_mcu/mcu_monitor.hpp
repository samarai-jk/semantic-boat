#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"

namespace can_mcu {

// Measures the MCU supply (VDDA = +3V3 on the can-mcu) through the internal
// voltage reference and the die temperature through the internal sensor,
// both with their factory calibration. run() reads a finished conversion and
// immediately starts the next, so the supply is sampled as fast as the main
// loop allows (one conversion takes about 8 us). Every 256th conversion
// measures the temperature instead, averaged over 16 readings.
//
// Supply minimum and maximum are tracked per window; takeWindow() returns
// and restarts it. The monitor currently owns ADC1 exclusively. Once other
// analog inputs are sampled, it moves behind a shared ADC sequencer.
class McuMonitor final : public slstm32::Driver {
public:
    struct SupplyWindow {
        std::uint16_t minimumMv{};
        std::uint16_t maximumMv{};
        std::uint32_t samples{};
    };

    bool init() override;
    void run() override;
    std::uint16_t supplyMv() const { return supplyMv_; }
    SupplyWindow takeWindow();
    bool temperatureValid() const { return temperatureValid_; }
    std::int32_t dieMilliCelsius() const { return dieMilliCelsius_; }

private:
    static constexpr std::uint32_t temperatureInterval = 256u;
    static constexpr std::uint32_t temperatureAverage = 16u;

    bool selectChannel(bool temperature);
    void resetWindow();

    SupplyWindow window_{};
    std::uint16_t supplyMv_{};
    std::uint32_t conversions_{};
    std::int32_t temperatureSum_{};
    std::uint32_t temperatureCount_{};
    std::int32_t dieMilliCelsius_{};
    bool temperatureValid_{};
    bool measuringTemperature_{};
    bool converting_{};
};

} // namespace can_mcu
