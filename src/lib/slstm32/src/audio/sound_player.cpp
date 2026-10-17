#include "slstm32/audio/sound_player.hpp"
#include <cmath>

namespace slstm32::audio {

namespace {
constexpr std::uint16_t full = Synthesizer::fullLevel;
constexpr std::uint32_t bellRingMs = 2500u;
} // namespace

bool SoundPlayer::init() {
    synth_.init();
    synth_.silence();
    return true;
}

void SoundPlayer::begin(Effect effect, std::uint32_t durationMs) {
    synth_.silence();
    effect_ = effect;
    durationMs_ = durationMs;
    startedAt_ = runtime_.millis();
    lastUpdate_ = startedAt_ - 1u;
}

void SoundPlayer::stop() {
    synth_.silence();
    effect_ = Effect::none;
}

void SoundPlayer::tone(std::uint32_t hz, std::uint32_t durationMs, Wave wave, std::uint16_t level) {
    begin(Effect::tone, durationMs);
    hz1_ = hz;
    wave_ = wave;
    level_ = level;
}

void SoundPlayer::wail(std::uint32_t durationMs, std::uint16_t lowHz, std::uint16_t highHz, std::uint32_t periodMs) {
    begin(Effect::wail, durationMs);
    hz1_ = lowHz;
    hz2_ = highHz;
    periodMs_ = periodMs ? periodMs : 1u;
}

void SoundPlayer::yelp(std::uint32_t durationMs, std::uint16_t lowHz, std::uint16_t highHz, std::uint32_t periodMs) {
    begin(Effect::yelp, durationMs);
    hz1_ = lowHz;
    hz2_ = highHz;
    periodMs_ = periodMs ? periodMs : 1u;
}

void SoundPlayer::twoTone(std::uint32_t durationMs, std::uint16_t firstHz, std::uint16_t secondHz,
                          std::uint32_t halfPeriodMs) {
    begin(Effect::twoTone, durationMs);
    hz1_ = firstHz;
    hz2_ = secondHz;
    periodMs_ = halfPeriodMs ? halfPeriodMs : 1u;
}

void SoundPlayer::beeps(std::uint32_t durationMs, std::uint8_t count, std::uint16_t hz, std::uint16_t onMs,
                        std::uint16_t offMs, std::uint32_t groupPeriodMs, Wave wave) {
    begin(Effect::beeps, durationMs);
    count_ = count;
    hz1_ = hz;
    onMs_ = onMs;
    offMs_ = offMs;
    periodMs_ = groupPeriodMs ? groupPeriodMs : 1u;
    wave_ = wave;
}

void SoundPlayer::generalAlarm(std::uint16_t hz, std::uint16_t shortMs, std::uint16_t longMs) {
    begin(Effect::generalAlarm, 7u * 2u * shortMs + longMs);
    hz1_ = hz;
    onMs_ = shortMs;
}

void SoundPlayer::chirps(std::uint8_t count, std::uint16_t fromHz, std::uint16_t toHz, std::uint16_t chirpMs,
                         std::uint16_t gapMs, std::uint16_t level) {
    begin(Effect::chirps, static_cast<std::uint32_t>(count) * (chirpMs + gapMs));
    hz1_ = fromHz;
    hz2_ = toHz;
    onMs_ = chirpMs ? chirpMs : 1u;
    offMs_ = gapMs;
    level_ = level;
}

void SoundPlayer::bells(const std::uint16_t* notesHz, std::size_t count, std::uint32_t intervalMs) {
    if (!notesHz || count == 0u) {
        stop();
        return;
    }
    begin(Effect::bells, static_cast<std::uint32_t>(count - 1u) * intervalMs + bellRingMs);
    notes_ = notesHz;
    count_ = static_cast<std::uint8_t>(count);
    periodMs_ = intervalMs;
}

void SoundPlayer::run() {
    if (effect_ == Effect::none) return;
    const auto now = runtime_.millis();
    if (now == lastUpdate_) return;
    lastUpdate_ = now;
    const auto t = now - startedAt_;
    if (t >= durationMs_) {
        stop();
        return;
    }
    update(t);
}

void SoundPlayer::update(std::uint32_t t) {
    switch (effect_) {
    case Effect::tone:
        synth_.set(0, hz1_, level_, wave_);
        return;
    case Effect::wail: {
        const float x = 0.5f - 0.5f * std::cos(6.2831853f * static_cast<float>(t % periodMs_) / static_cast<float>(periodMs_));
        const auto span = static_cast<float>(static_cast<std::int32_t>(hz2_) - static_cast<std::int32_t>(hz1_));
        synth_.set(0, static_cast<std::uint32_t>(static_cast<float>(hz1_) + span * x), full);
        return;
    }
    case Effect::yelp:
        synth_.set(0, static_cast<std::uint32_t>(static_cast<std::int32_t>(hz1_) +
                                                 static_cast<std::int32_t>(t % periodMs_) *
                                                     (static_cast<std::int32_t>(hz2_) - static_cast<std::int32_t>(hz1_)) /
                                                     static_cast<std::int32_t>(periodMs_)),
                   full);
        return;
    case Effect::twoTone:
        synth_.set(0, (t / periodMs_) % 2u ? hz2_ : hz1_, full);
        return;
    case Effect::beeps: {
        const auto inGroup = t % periodMs_;
        const auto slot = onMs_ + offMs_;
        const bool on = inGroup < count_ * slot && (inGroup % slot) < onMs_;
        synth_.set(0, hz1_, on ? full : 0u, wave_);
        return;
    }
    case Effect::generalAlarm: {
        const auto shortsEnd = 7u * 2u * onMs_;
        const bool on = t < shortsEnd ? (t % (2u * onMs_)) < onMs_ : true;
        synth_.set(0, hz1_, on ? full : 0u, Wave::square);
        return;
    }
    case Effect::chirps: {
        const auto inChirp = t % (onMs_ + offMs_);
        const bool on = inChirp < onMs_;
        const auto hz = static_cast<std::int32_t>(hz1_) +
                        static_cast<std::int32_t>(inChirp) * (static_cast<std::int32_t>(hz2_) - static_cast<std::int32_t>(hz1_)) /
                            static_cast<std::int32_t>(onMs_);
        synth_.set(0, static_cast<std::uint32_t>(hz), on ? level_ : 0u);
        return;
    }
    case Effect::bells: {
        std::size_t current = t / periodMs_;
        if (current >= count_) current = count_ - 1u;
        const float since = static_cast<float>(t - current * periodMs_) / 1000.0f;
        const auto hz = notes_[current];
        const auto level = [since](float decay, float share) {
            return static_cast<std::uint16_t>(32767.0f * share * std::exp(-since / decay));
        };
        synth_.set(0, hz, level(0.8f, 0.6f));
        synth_.set(1, static_cast<std::uint32_t>(static_cast<float>(hz) * 2.76f), level(0.3f, 0.25f));
        synth_.set(2, static_cast<std::uint32_t>(static_cast<float>(hz) * 5.40f), level(0.12f, 0.15f));
        return;
    }
    default:
        return;
    }
}

} // namespace slstm32::audio
