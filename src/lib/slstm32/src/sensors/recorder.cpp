#include "slstm32/sensors/recorder.hpp"

namespace slstm32::sensors {

namespace {

bool reached(std::uint32_t now, std::uint32_t at) { return static_cast<std::int32_t>(now - at) >= 0; }

void put16(std::uint8_t* out, std::uint16_t value) {
    out[0] = static_cast<std::uint8_t>(value);
    out[1] = static_cast<std::uint8_t>(value >> 8u);
}

void put32(std::uint8_t* out, std::uint32_t value) {
    for (std::size_t index = 0u; index < 4u; ++index) out[index] = static_cast<std::uint8_t>(value >> (8u * index));
}

constexpr std::size_t segmentHeaderBytes = 8u; // offset, tick, ticks; then the multiples

} // namespace

std::int16_t Recorder::sample(std::size_t index) const {
    const auto& series = series_[index];
    std::int32_t value{};
    if (!series.read || !series.read(series.context, value)) return missing;
    value /= series.divisor != 0u ? static_cast<std::int32_t>(series.divisor) : 1;
    if (value > INT16_MAX) return INT16_MAX;
    if (value <= missing) return static_cast<std::int16_t>(missing + 1);
    return static_cast<std::int16_t>(value);
}

void Recorder::sampleLeadIn(std::uint32_t now) {
    const auto frameBytes = 2u * count_;
    const auto capacity = frameBytes != 0u ? leadInBytes_ / frameBytes : 0u;
    if (capacity == 0u || !reached(now, leadInNextAt_)) return;
    leadInNextAt_ = now + leadInIntervalMs_;
    auto* frame = leadIn_ + leadInHead_ * frameBytes;
    for (std::size_t index = 0u; index < count_; ++index) {
        put16(frame + 2u * index, static_cast<std::uint16_t>(sample(index)));
    }
    leadInHead_ = (leadInHead_ + 1u) % capacity;
    if (leadInFrames_ < capacity) ++leadInFrames_;
}

bool Recorder::start() {
    if (count_ == 0u || count_ > maxSeries || !arena_) return false;
    const auto now = runtime_.millis();
    ++number_;
    state_ = State::recording;
    endReason_ = EndReason::none;
    truncated_ = false;
    startedAt_ = now;
    durationMs_ = 0u;
    events_ = 0u;
    used_ = 0u;
    sampling_ = false;
    // The lead-in, oldest frame first, as a segment before the start.
    const auto frameBytes = 2u * count_;
    const auto capacity = frameBytes != 0u ? leadInBytes_ / frameBytes : 0u;
    if (leadInFrames_ != 0u && segmentHeaderBytes + count_ + leadInFrames_ * frameBytes <= sampleCapacity()) {
        auto* out = arena_ + eventCapacity_ * eventBytes;
        put32(out, static_cast<std::uint32_t>(-static_cast<std::int32_t>(leadInFrames_ * leadInIntervalMs_)));
        put16(out + 4u, leadInIntervalMs_);
        put16(out + 6u, static_cast<std::uint16_t>(leadInFrames_));
        for (std::size_t index = 0u; index < count_; ++index) out[segmentHeaderBytes + index] = 1u;
        used_ = segmentHeaderBytes + count_;
        const auto oldest = (leadInHead_ + capacity - leadInFrames_) % capacity;
        for (std::size_t frame = 0u; frame < leadInFrames_; ++frame) {
            const auto* source = leadIn_ + ((oldest + frame) % capacity) * frameBytes;
            for (std::size_t byte = 0u; byte < frameBytes; ++byte) out[used_ + byte] = source[byte];
            used_ += frameBytes;
        }
    }
    leadInFrames_ = 0u;
    leadInHead_ = 0u;
    return true;
}

bool Recorder::setPhase(const std::uint16_t* intervalsMs) {
    if (state_ != State::recording || !intervalsMs) return false;
    std::uint16_t tick{};
    for (std::size_t index = 0u; index < count_; ++index) {
        if (intervalsMs[index] != 0u && (tick == 0u || intervalsMs[index] < tick)) tick = intervalsMs[index];
    }
    if (tick == 0u) {
        sampling_ = false;
        return true;
    }
    std::uint8_t multiples[maxSeries]{};
    for (std::size_t index = 0u; index < count_; ++index) {
        const auto interval = intervalsMs[index];
        if (interval == 0u) continue;
        if (interval % tick != 0u || interval / tick > 255u) return false;
        multiples[index] = static_cast<std::uint8_t>(interval / tick);
    }
    if (used_ + segmentHeaderBytes + count_ > sampleCapacity()) {
        finish(EndReason::full);
        return false;
    }
    const auto now = runtime_.millis();
    auto* out = arena_ + eventCapacity_ * eventBytes + used_;
    segment_ = used_;
    put32(out, now - startedAt_);
    put16(out + 4u, tick);
    put16(out + 6u, 0u);
    for (std::size_t index = 0u; index < count_; ++index) {
        out[segmentHeaderBytes + index] = multiples[index];
        multiples_[index] = multiples[index];
    }
    used_ += segmentHeaderBytes + count_;
    tickMs_ = tick;
    ticks_ = 0u;
    nextTickAt_ = now;
    sampling_ = true;
    return true;
}

bool Recorder::writeFrame() {
    auto* samples = arena_ + eventCapacity_ * eventBytes;
    std::size_t bytes{};
    for (std::size_t index = 0u; index < count_; ++index) {
        if (multiples_[index] != 0u && ticks_ % multiples_[index] == 0u) bytes += 2u;
    }
    if (ticks_ == UINT16_MAX || used_ + bytes > sampleCapacity()) return false;
    for (std::size_t index = 0u; index < count_; ++index) {
        if (multiples_[index] == 0u || ticks_ % multiples_[index] != 0u) continue;
        put16(samples + used_, static_cast<std::uint16_t>(sample(index)));
        used_ += 2u;
    }
    ++ticks_;
    put16(samples + segment_ + 6u, ticks_);
    return true;
}

bool Recorder::mark(std::uint8_t id, std::uint8_t kind, std::uint8_t aux, std::int32_t value) {
    if (state_ != State::recording) return false;
    if (events_ == eventCapacity_) {
        truncated_ = true;
        return false;
    }
    auto* out = arena_ + events_ * eventBytes;
    put32(out, runtime_.millis() - startedAt_);
    out[4] = id;
    out[5] = kind;
    out[6] = aux;
    out[7] = 0u;
    put32(out + 8u, static_cast<std::uint32_t>(value));
    ++events_;
    return true;
}

void Recorder::finish(EndReason reason) {
    if (state_ != State::recording) return;
    state_ = State::complete;
    endReason_ = reason;
    if (reason == EndReason::full) truncated_ = true;
    durationMs_ = runtime_.millis() - startedAt_;
    sampling_ = false;
    leadInNextAt_ = runtime_.millis();
}

void Recorder::release() {
    if (state_ == State::complete) state_ = State::idle;
}

std::uint32_t Recorder::durationMs() const {
    return state_ == State::recording ? runtime_.millis() - startedAt_ : durationMs_;
}

void Recorder::run() {
    const auto now = runtime_.millis();
    if (state_ != State::recording) {
        // The lead-in keeps running while a complete recording waits to be fetched.
        sampleLeadIn(now);
        return;
    }
    if (!sampling_) return;
    // Every tick that is due; a late call samples the current values for the
    // missed ticks, so the time of every sample stays its tick.
    for (std::size_t guard = 0u; guard < 64u && reached(now, nextTickAt_); ++guard) {
        if (!writeFrame()) {
            finish(EndReason::full);
            return;
        }
        nextTickAt_ += tickMs_;
    }
    if (reached(now, nextTickAt_)) {
        // Far behind (stalled): skip ahead with a new segment rather than
        // writing a burst of copies.
        std::uint16_t intervals[maxSeries]{};
        for (std::size_t index = 0u; index < count_; ++index) {
            intervals[index] = static_cast<std::uint16_t>(multiples_[index] * tickMs_);
        }
        (void)setPhase(intervals);
    }
}

} // namespace slstm32::sensors
