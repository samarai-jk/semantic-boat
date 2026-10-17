#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/hardware.hpp"

namespace can_mcu {

// Measures the MCU supply (VDDA = +3V3 on the can-mcu) through the internal
// voltage reference and the die temperature through the internal sensor,
// both with their factory calibration. run() reads a finished conversion and
// immediately starts the next, so the supply is sampled as fast as the main
// loop allows (one conversion takes about 8 us). Every 256th conversion
// measures the temperature instead, averaged over 16 readings.
//
// Supply minimum and maximum are tracked per window; takeWindow() returns
// and restarts it.
//
// The monitor owns ADC1 and also samples external analog inputs: every 32nd
// conversion measures one of them (in turn), and 16 readings are averaged
// into a voltage scaled with the measured supply. With a fast main loop each
// input gets a new value several times per second.
class McuMonitor final : public slstm32::Driver {
public:
    static constexpr std::size_t maxInputs = 4u;

    struct SupplyWindow {
        std::uint16_t minimumMv{};
        std::uint16_t maximumMv{};
        std::uint32_t samples{};
    };

    bool init() override;
    void run() override;
    std::uint16_t supplyMv() const { return supplyMv_; }
    SupplyWindow takeWindow();
    // Lowest supply reading since init(), never reset.
    std::uint16_t lowestMv() const { return lowestMv_; }
    bool temperatureValid() const { return temperatureValid_; }
    std::int32_t dieMilliCelsius() const { return dieMilliCelsius_; }

    // Adds an external input by its ADC1 channel number (PA0..PA3 are
    // IN5..IN8, PA4..PA7 IN9..IN12, PB0/PB1 IN15/IN16). Call before init();
    // the pin must be configured as analog. Returns the input index, or
    // maxInputs when full.
    std::size_t addInput(std::uint8_t adcChannel);
    bool inputValid(std::size_t index) const { return index < inputCount_ && inputs_[index].valid; }
    std::int32_t inputMillivolts(std::size_t index) const { return inputs_[index].millivolts; }
    // Binding for analog sensor drivers: sample() yields the input in mV,
    // false until the first average is complete. May be taken before
    // addInput() (bindings are usually built with the drivers).
    slstm32::drivers::AnalogInputHardware inputHardware(std::size_t index);

private:
    static constexpr std::uint32_t temperatureInterval = 256u;
    static constexpr std::uint32_t temperatureAverage = 16u;
    static constexpr std::uint32_t inputInterval = 32u;
    static constexpr std::uint32_t inputAverage = 16u;

    enum class Source : std::uint8_t { supply, temperature, input };
    struct Input {
        McuMonitor* monitor{};
        std::uint8_t channel{};
        std::int32_t sum{};
        std::uint32_t count{};
        std::int32_t millivolts{};
        bool valid{};
    };

    static bool sampleInput(void* context, std::int32_t& value);
    bool select(Source source, std::size_t input = 0u);
    void resetWindow();

    SupplyWindow window_{};
    std::uint16_t supplyMv_{};
    std::uint16_t lowestMv_{0xffffu};
    std::uint32_t conversions_{};
    std::int32_t temperatureSum_{};
    std::uint32_t temperatureCount_{};
    std::int32_t dieMilliCelsius_{};
    bool temperatureValid_{};
    Input inputs_[maxInputs]{};
    std::size_t inputCount_{};
    std::size_t nextInput_{};
    std::size_t measuringInput_{};
    Source source_{Source::supply};
    bool converting_{};
};

} // namespace can_mcu
