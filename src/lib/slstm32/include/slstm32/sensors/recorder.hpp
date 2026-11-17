#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/runtime.hpp"

namespace slstm32::sensors {

// One value a Recorder samples: read() gives it in thousandths of its unit
// (as Measurement values); the recorder stores value / divisor as int16
// (clamped; a failed read is Recorder::missing).
struct RecorderSeries {
    std::uint8_t id{};         // the application's identifier (B1S: the channel number)
    std::uint16_t divisor{1u}; // e.g. 1000 for rpm (0.001 rpm -> 1 rpm)
    bool (*read)(void* context, std::int32_t& value){};
    void* context{};
};

// Records several values at rates that change with the situation, for a
// diagnosis after the fact (an engine start, a pump run). Fixed memory, no
// heap; the application decides when a recording starts, which rates apply
// (phases) and when it ends; the recorder samples on time.
//
// - While idle, every series is sampled every leadInIntervalMs into a small
//   ring: a recording starts with the moments before its trigger.
// - A phase gives every series its interval (0 = not recorded); the
//   shortest interval is the phase's tick, the others must be multiples of
//   it. Each phase change starts a new segment.
// - Events (an input changing, a value at the start) carry a time, the
//   application's id, kind and aux bytes and an int32 value.
// - A recording ends with finish() (or when the memory is full) and is kept,
//   complete, until release() or the next start().
//
// Memory layout (little-endian; read with events() and samples()):
// - events: 12 bytes each: offset ms (int32, from the start), id, kind, aux,
//   0, value (int32);
// - samples: segments, each: offset ms of its first tick (int32, the lead-in
//   negative), tick ms (uint16), ticks (uint16), one multiple per series
//   (uint8: the series is sampled every multiple ticks, 0 = not in the
//   segment), then for each tick k the int16 samples of the series whose
//   multiple divides k, in series order.
class Recorder {
public:
    static constexpr std::size_t maxSeries = 12u;
    static constexpr std::size_t eventBytes = 12u;
    static constexpr std::int16_t missing = INT16_MIN;
    enum class State : std::uint8_t { idle, recording, complete };
    enum class EndReason : std::uint8_t { none, finished, timeout, full, aborted };

    // arena: eventCapacity * eventBytes for events, the rest for samples.
    // leadIn: the ring of the idle samples (2 bytes per series and frame).
    Recorder(Runtime runtime, const RecorderSeries* series, std::size_t count, std::uint8_t* arena,
             std::size_t arenaBytes, std::size_t eventCapacity, std::uint8_t* leadIn, std::size_t leadInBytes,
             std::uint16_t leadInIntervalMs)
        : runtime_(runtime), series_(series), count_(count), arena_(arena), arenaBytes_(arenaBytes),
          eventCapacity_(eventCapacity), leadIn_(leadIn), leadInBytes_(leadInBytes), leadInIntervalMs_(leadInIntervalMs) {}

    // Starts a recording (dropping a complete one not yet released): the
    // lead-in becomes the first segment. Sampling begins with setPhase().
    bool start();
    // Intervals in ms per series (count values). Starts a new segment.
    bool setPhase(const std::uint16_t* intervalsMs);
    bool mark(std::uint8_t id, std::uint8_t kind, std::uint8_t aux, std::int32_t value);
    void finish(EndReason reason);
    void release();
    void run();

    State state() const { return state_; }
    EndReason endReason() const { return endReason_; }
    bool truncated() const { return truncated_; }
    std::size_t seriesCount() const { return count_; }
    const RecorderSeries& series(std::size_t index) const { return series_[index]; }
    // Time since the start (recording) or the recording's length (complete).
    std::uint32_t durationMs() const;
    std::uint32_t elapsedMs() const { return runtime_.millis() - startedAt_; }
    std::uint16_t recordingNumber() const { return number_; }
    std::size_t eventCount() const { return events_; }
    const std::uint8_t* events() const { return arena_; }
    const std::uint8_t* samples() const { return arena_ + eventCapacity_ * eventBytes; }
    std::size_t sampleBytes() const { return used_; }

private:
    void sampleLeadIn(std::uint32_t now);
    bool writeFrame();
    std::int16_t sample(std::size_t index) const;
    std::size_t sampleCapacity() const { return arenaBytes_ - eventCapacity_ * eventBytes; }

    Runtime runtime_;
    const RecorderSeries* series_{};
    std::size_t count_{};
    std::uint8_t* arena_{};
    std::size_t arenaBytes_{};
    std::size_t eventCapacity_{};
    std::uint8_t* leadIn_{};
    std::size_t leadInBytes_{};
    std::uint16_t leadInIntervalMs_{};
    State state_{State::idle};
    EndReason endReason_{EndReason::none};
    bool truncated_{};
    std::uint16_t number_{};
    std::uint32_t startedAt_{};
    std::uint32_t durationMs_{};
    std::size_t events_{};
    std::size_t used_{};
    // The open segment: its header offset, tick, multiples, tick count.
    std::size_t segment_{};
    std::uint16_t tickMs_{};
    std::uint8_t multiples_[maxSeries]{};
    std::uint16_t ticks_{};
    std::uint32_t nextTickAt_{};
    bool sampling_{};
    // Lead-in ring.
    std::size_t leadInHead_{};
    std::size_t leadInFrames_{};
    std::uint32_t leadInNextAt_{};
};

} // namespace slstm32::sensors
