#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/audio/synthesizer.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace slstm32::audio {

// Alarm sounds, signals, and chimes on a Synthesizer, updated every
// millisecond from run(). Starting a sound replaces the one playing. The
// defaults are the values tried on the bench (lab-test-01, DFRobot FIT0449
// speaker): see slstm32/SENSORS.md.
class SoundPlayer final : public Service {
public:
    SoundPlayer(Runtime runtime, Synthesizer& synthesizer) : runtime_(runtime), synth_(synthesizer) {}

    bool init() override;
    void run() override;

    void tone(std::uint32_t hz, std::uint32_t durationMs, Wave wave = Wave::sine,
              std::uint16_t level = Synthesizer::fullLevel);
    // Siren sweeping between two frequencies: wail (slow, cosine-shaped,
    // period 4 s) and yelp (fast rising sawtooth, period 250 ms).
    void wail(std::uint32_t durationMs, std::uint16_t lowHz = 600u, std::uint16_t highHz = 1300u,
              std::uint32_t periodMs = 4000u);
    void yelp(std::uint32_t durationMs, std::uint16_t lowHz = 600u, std::uint16_t highHz = 1300u,
              std::uint32_t periodMs = 250u);
    // Alternating two-tone ("hi-lo"), e.g. 960 / 770 Hz.
    void twoTone(std::uint32_t durationMs, std::uint16_t firstHz = 960u, std::uint16_t secondHz = 770u,
                 std::uint32_t halfPeriodMs = 500u);
    // Groups of beeps: count beeps of onMs with offMs gaps, groups repeated
    // every groupPeriodMs for durationMs.
    void beeps(std::uint32_t durationMs, std::uint8_t count = 3u, std::uint16_t hz = 2000u, std::uint16_t onMs = 150u,
               std::uint16_t offMs = 150u, std::uint32_t groupPeriodMs = 2000u, Wave wave = Wave::square);
    // Ship's general alarm: 7 short and 1 long signal.
    void generalAlarm(std::uint16_t hz = 1000u, std::uint16_t shortMs = 300u, std::uint16_t longMs = 1500u);
    // Rising chirps ("have a look").
    void chirps(std::uint8_t count = 2u, std::uint16_t fromHz = 1500u, std::uint16_t toHz = 3000u,
                std::uint16_t chirpMs = 60u, std::uint16_t gapMs = 90u, std::uint16_t level = Synthesizer::fullLevel / 4u);
    // Bell strikes: one note every intervalMs (fundamental plus two decaying
    // inharmonic partials). The notes array must outlive the sound.
    void bells(const std::uint16_t* notesHz, std::size_t count, std::uint32_t intervalMs = 700u);

    void stop();
    bool playing() const { return effect_ != Effect::none; }

private:
    enum class Effect : std::uint8_t { none, tone, wail, yelp, twoTone, beeps, generalAlarm, chirps, bells };
    void begin(Effect effect, std::uint32_t durationMs);
    void update(std::uint32_t t);

    Runtime runtime_;
    Synthesizer& synth_;
    Effect effect_{Effect::none};
    std::uint32_t startedAt_{};
    std::uint32_t lastUpdate_{};
    std::uint32_t durationMs_{};
    // Parameters of the current effect.
    std::uint32_t hz1_{};
    std::uint32_t hz2_{};
    std::uint32_t periodMs_{};
    std::uint32_t onMs_{};
    std::uint32_t offMs_{};
    std::uint16_t level_{};
    std::uint8_t count_{};
    Wave wave_{Wave::sine};
    const std::uint16_t* notes_{};
};

} // namespace slstm32::audio
