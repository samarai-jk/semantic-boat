#include "semantic_modules/ad_input_iso_12v.hpp"

namespace semantic_modules {

using slstm32::drivers::Ads1115Driver;
using slstm32::drivers::DigitalInputDriver;
using slstm32::drivers::DigitalInputHardware;

DigitalInputHardware AdInputIso12v::activeLow(DigitalInputHardware hardware) {
    hardware.activeHigh = false;
    return hardware;
}

AdInputIso12v::AdInputIso12v(slstm32::Runtime runtime, slstm32::drivers::I2cBus& bus,
                             const AdInputIso12vConfig& config)
    : config_(config),
      adcChannels_{{
          {Ads1115Driver::Input::ain0, config.gain},
          {Ads1115Driver::Input::ain1, config.gain},
          {Ads1115Driver::Input::ain2, config.gain},
          {Ads1115Driver::Input::ain3, config.gain},
      }},
      adc_(runtime, bus, config.adcAddress, adcChannels_.data(), adcChannels_.size(), config.dataRate),
      inputs_{{
          DigitalInputDriver{runtime, activeLow(config.digitalInputs[0]), config.debounceMs},
          DigitalInputDriver{runtime, activeLow(config.digitalInputs[1]), config.debounceMs},
          DigitalInputDriver{runtime, activeLow(config.digitalInputs[2]), config.debounceMs},
          DigitalInputDriver{runtime, activeLow(config.digitalInputs[3]), config.debounceMs},
      }} {}

bool AdInputIso12v::init() {
    bool ok = adc_.init();
    for (std::size_t channel = 0u; channel < channels; ++channel) {
        if (digitalConnected(channel)) ok = inputs_[channel].init() && ok;
    }
    return ok;
}

void AdInputIso12v::run() {
    adc_.run();
    for (std::size_t channel = 0u; channel < channels; ++channel) {
        if (digitalConnected(channel)) inputs_[channel].run();
    }
}

std::int32_t AdInputIso12v::analogMillivolts(std::size_t channel) const {
    if (channel >= channels) return 0;
    return static_cast<std::int32_t>(static_cast<float>(adc_.microvolts(channel)) *
                                     calibration_.scale[channel] / 1000.0f) -
           calibration_.offsetMv[channel];
}

} // namespace semantic_modules
