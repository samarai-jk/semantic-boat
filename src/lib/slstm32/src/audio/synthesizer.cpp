#include "slstm32/audio/synthesizer.hpp"
#include <cmath>

namespace slstm32::audio {

Synthesizer::Synthesizer(std::uint32_t sampleRate) : sampleRate_(sampleRate ? sampleRate : 1u) {}

void Synthesizer::init() {
    for (std::size_t index = 0u; index < 256u; ++index) {
        sine_[index] = static_cast<std::int16_t>(std::lround(32767.0f * std::sin(static_cast<float>(index) * 6.2831853f / 256.0f)));
    }
}

void Synthesizer::set(std::size_t oscillator, std::uint32_t hz, std::uint16_t level, Wave wave) {
    if (oscillator >= oscillatorCount) return;
    auto& target = oscillators_[oscillator];
    target.step = static_cast<std::uint32_t>((static_cast<std::uint64_t>(hz) << 32u) / sampleRate_);
    target.wave = wave;
    target.level = level;
}

void Synthesizer::silence() {
    for (auto& oscillator : oscillators_) oscillator.level = 0u;
}

std::int16_t Synthesizer::nextSample() {
    std::int32_t mix = 0;
    for (auto& oscillator : oscillators_) {
        const std::uint16_t level = oscillator.level;
        if (level == 0u) continue;
        oscillator.phase += oscillator.step;
        const auto phase = oscillator.phase;
        std::int32_t sample;
        switch (oscillator.wave) {
        case Wave::square: sample = phase & 0x80000000u ? -32767 : 32767; break;
        case Wave::triangle: {
            const auto ramp = static_cast<std::int32_t>(phase >> 16u);
            sample = ramp < 32768 ? ramp * 2 - 32767 : (65535 - ramp) * 2 - 32767;
            break;
        }
        default: sample = sine_[phase >> 24u]; break;
        }
        mix += sample * level >> 15;
    }
    if (mix > 32767) mix = 32767;
    if (mix < -32767) mix = -32767;
    return static_cast<std::int16_t>(mix * volume_ >> 15);
}

} // namespace slstm32::audio
