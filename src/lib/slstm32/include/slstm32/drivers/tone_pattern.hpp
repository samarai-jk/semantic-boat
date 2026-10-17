#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/drivers/tone.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace slstm32::drivers {

// One step of a tone pattern; frequency 0 is a pause.
struct ToneStep {
    std::uint16_t frequencyHz{};
    std::uint16_t durationMs{};
    float level{1.0f};
};

struct TonePattern {
    const ToneStep* steps{};
    std::size_t count{};
    bool repeat{}; // start over after the last step until stop()
};

// Plays tone patterns (beep sequences, sirens) on a ToneDriver. Patterns are
// usually constexpr tables in flash; the player keeps only a pointer.
class TonePatternPlayer final : public Service {
public:
    TonePatternPlayer(Runtime runtime, ToneDriver& tone) : runtime_(runtime), tone_(tone) {}

    bool init() override { return true; }
    void run() override;
    // Starts the pattern from its first step; replaces a playing pattern.
    void play(const TonePattern& pattern);
    void stop();
    bool playing() const { return pattern_ != nullptr; }
    const TonePattern* pattern() const { return pattern_; }

private:
    void startStep();

    Runtime runtime_;
    ToneDriver& tone_;
    const TonePattern* pattern_{};
    std::size_t step_{};
    std::uint32_t stepEndsAt_{};
};

} // namespace slstm32::drivers
