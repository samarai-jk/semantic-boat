#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/ads1115.hpp"
#include "slstm32/drivers/digital_input.hpp"
#include "slstm32/drivers/hardware.hpp"
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/runtime.hpp"

namespace semantic_modules {

// Per-channel analog calibration: terminal mV = ADC uV * scale / 1000 -
// offset mV. The design values are scale 9.2 ((82k + 10k) / 10k), offset 0.
struct AdInputIso12vCalibration {
    static constexpr float designScale = 9.2f;
    std::array<float, 4> scale{designScale, designScale, designScale, designScale};
    std::array<std::int16_t, 4> offsetMv{};
};

// Configuration of a 12v-ad-input-iso module. See ../../12v-ad-input-iso.md.
struct AdInputIso12vConfig {
    static constexpr std::size_t channels = 4u;

    // 0x48 with R33 fitted (ADDR to GND), 0x49 with R34 fitted (ADDR to VDD).
    std::uint8_t adcAddress{slstm32::drivers::Ads1115Driver::baseAddress};
    // ADC full-scale range. With the 82k/10k divider, fsr2048 measures up to
    // 18.8 V at the terminal (12 V systems), fsr4096 up to 37.7 V (input
    // protection limits the usable range to about 24 V).
    slstm32::drivers::Ads1115Driver::Gain gain{slstm32::drivers::Ads1115Driver::Gain::fsr2048};
    slstm32::drivers::Ads1115Driver::DataRate dataRate{slstm32::drivers::Ads1115Driver::DataRate::sps128};
    // MCU pins wired to the module's OUT_D_0..3 (header H1). Each binding
    // reports the physical pin level; the module drives the pin low while
    // 12 V is present, which the driver accounts for. A binding without a
    // read callback marks the input as not connected.
    std::array<slstm32::drivers::DigitalInputHardware, channels> digitalInputs{};
    // Stable time before a digital input change is accepted. The module's RC
    // filter adds about 2 ms; mechanical contacts need more.
    std::uint32_t debounceMs{20u};
};

// Isolated 12 V input module: four analog inputs through an ADS1115 behind an
// I2C isolator and four digital inputs through a digital isolator.
//
// Analog channels are scanned continuously without blocking; values are
// reported as the voltage at the terminal. Digital inputs are debounced;
// "active" means 12 V is present at the terminal. A missing module does not
// fail init(): analog channels report !analogValid() until the ADC answers.
class AdInputIso12v final : public slstm32::Driver {
public:
    static constexpr std::size_t channels = AdInputIso12vConfig::channels;

    AdInputIso12v(slstm32::Runtime runtime, slstm32::drivers::I2cBus& bus,
                  const AdInputIso12vConfig& config);

    bool init() override;
    void run() override;

    bool adcPresent() const { return adc_.present(); }
    std::uint32_t adcErrors() const { return adc_.errors(); }
    std::uint8_t adcAddress() const { return adc_.address(); }

    // Starts with the design values; a stored calibration is applied with
    // setCalibration() (see AdInputIso12vCalibrator).
    void setCalibration(const AdInputIso12vCalibration& calibration) { calibration_ = calibration; }
    const AdInputIso12vCalibration& calibration() const { return calibration_; }

    bool analogValid(std::size_t channel) const { return adc_.valid(channel); }
    std::int32_t analogMillivolts(std::size_t channel) const;
    std::int32_t adcMicrovolts(std::size_t channel) const { return adc_.microvolts(channel); }
    std::uint32_t analogSamples(std::size_t channel) const { return adc_.samples(channel); }

    bool digitalConnected(std::size_t channel) const {
        return channel < channels && config_.digitalInputs[channel].read != nullptr;
    }
    bool digitalActive(std::size_t channel) const {
        return digitalConnected(channel) && inputs_[channel].value();
    }
    std::uint32_t digitalChanges(std::size_t channel) const {
        return digitalConnected(channel) ? inputs_[channel].changes() : 0u;
    }

private:
    static slstm32::drivers::DigitalInputHardware activeLow(slstm32::drivers::DigitalInputHardware hardware);

    const AdInputIso12vConfig& config_;
    AdInputIso12vCalibration calibration_{};
    std::array<slstm32::drivers::Ads1115Driver::Channel, channels> adcChannels_;
    slstm32::drivers::Ads1115Driver adc_;
    std::array<slstm32::drivers::DigitalInputDriver, channels> inputs_;
};

} // namespace semantic_modules
