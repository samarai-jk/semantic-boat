#pragma once

#include <cstddef>
#include <cstdint>

namespace slstm32::audio {

enum class Wave : std::uint8_t { sine, square, triangle };

// A small additive synthesizer for alarm sounds and chimes: a few
// oscillators (sine from a table, square, triangle), each with its own
// frequency and level, mixed and scaled by a master volume.
//
// nextSample() runs in the sample interrupt (integer only, a few dozen
// cycles per oscillator); everything else is called from the main loop.
// Writes of one oscillator's frequency and level are not atomic as a pair;
// for sound effects updated every millisecond that is inaudible.
class Synthesizer {
public:
    static constexpr std::size_t oscillatorCount = 3u;
    static constexpr std::uint16_t fullLevel = 32767u;

    explicit Synthesizer(std::uint32_t sampleRate);

    // Builds the sine table (call once before use).
    void init();
    void set(std::size_t oscillator, std::uint32_t hz, std::uint16_t level, Wave wave = Wave::sine);
    void silence();
    // 0..fullLevel; applied to the mix.
    void setVolume(std::uint16_t volume) { volume_ = volume; }
    std::uint16_t volume() const { return volume_; }
    std::uint32_t sampleRate() const { return sampleRate_; }

    // Next output sample, -32767..32767 (interrupt context).
    std::int16_t nextSample();

private:
    struct Oscillator {
        volatile std::uint32_t step{};  // phase increment per sample, 2^32 = one period
        volatile std::uint16_t level{};
        volatile Wave wave{Wave::sine};
        std::uint32_t phase{};
    };

    std::uint32_t sampleRate_{};
    volatile std::uint16_t volume_{fullLevel};
    Oscillator oscillators_[oscillatorCount]{};
    std::int16_t sine_[256]{};
};

} // namespace slstm32::audio
