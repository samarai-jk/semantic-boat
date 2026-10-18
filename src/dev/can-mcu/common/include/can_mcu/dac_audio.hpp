#pragma once

#include <cstdint>
#include "slstm32/audio/synthesizer.hpp"

namespace can_mcu {

// Audio output of a Synthesizer on the STM32L431's 12-bit DAC: channel 1 on
// PA4 or channel 2 on PA5 (output buffer on, for an amplifier input such as
// the DFRobot FIT0449's 8002), one sample per TIM6 update interrupt at the
// synthesizer's sample rate. This layer owns TIM6 and TIM6_DAC_IRQHandler;
// do not enable TIM6 or the DAC in CubeMX. One output per device.
//
// Cost at 16 kHz: about 3 % CPU with three active oscillators. Status: the
// same register setup ran as dev code on lab-test-01 (git history, commit
// 6f15ce6); this class itself has not run on hardware yet.
class DacAudio {
public:
    enum class Pin : std::uint8_t { pa4, pa5 };

    DacAudio(slstm32::audio::Synthesizer& synthesizer, Pin pin) : synth_(synthesizer), pin_(pin) {}
    // Configures the pin, DAC, and TIM6 and starts the sample interrupt.
    bool start();
    void stop();

private:
    friend void dacAudioSample();

    slstm32::audio::Synthesizer& synth_;
    Pin pin_;
};

} // namespace can_mcu
