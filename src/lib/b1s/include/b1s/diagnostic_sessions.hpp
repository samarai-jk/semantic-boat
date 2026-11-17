#pragma once

#include <cstddef>
#include <cstdint>
#include "b1s/registry.hpp"
#include "slstm32/sensors/measurement.hpp"
#include "slstm32/sensors/recorder.hpp"

namespace b1s {

struct ChannelConfig;

// A device's diagnostic session on B1S (DIAGNOSTICS.md): a high-rate
// recording (slstm32 Recorder) of a situation, kept on the device until the
// Pi has fetched it with the diagnosticSession services. The device's
// policy decides when a session starts, its phases and its end; this class
// adds the B1S meaning: the recorder series are B1S channels with their
// quantities (in the order of the kind's series roles, registry), events
// carry channel states with their input role, snapshots (aux: the
// quantity) and phase codes (id 0xFF, aux: the phase), and
// the session is served as one byte stream: header, events, samples.
//
// Header (version 1, little-endian): "B1D", version, kind, flags (bit 0
// truncated), end reason, series count, session number (uint16), event
// count (uint16), start (unix seconds, uint32; 0 = clock not set), duration
// (ms, uint32), sample bytes (uint32); then per series: channel, quantity
// (registry code), divisor (uint16), SI per stored unit and SI offset
// (float32 each: SI value = sample x scale + offset). Events and samples as
// the Recorder lays them out.
class DiagnosticSessions {
public:
    enum class EventKind : std::uint8_t { state = 0u, snapshot = 1u, phase = 2u };
    static constexpr std::size_t headerBytes = 24u;
    static constexpr std::size_t seriesBytes = 12u;
    static constexpr std::size_t maxReadBytes = 200u;

    // quantities: one per recorder series, in its order (the series ids are
    // the B1S channel numbers). unixTime: the clock (0 = not set).
    DiagnosticSessions(slstm32::sensors::Recorder& recorder, DiagnosticSession kind,
                       const slstm32::sensors::Quantity* quantities, std::uint32_t (*unixTime)())
        : recorder_(recorder), kind_(kind), quantities_(quantities), unixTime_(unixTime) {}

    slstm32::sensors::Recorder& recorder() { return recorder_; }
    bool start();
    // A phase (the kind's phase code, 1 = the first) with the series' intervals.
    bool phase(std::uint8_t code, const std::uint16_t* intervalsMs);
    // A switch input's state; role: its position in the kind's inputs (1 = the first, 0 = none).
    void state(std::uint8_t channel, bool on, std::uint8_t role);
    // The channel's current output, as its B1S value (Measurement).
    void snapshot(const ChannelConfig& channel);
    void finish(slstm32::sensors::Recorder::EndReason reason) { recorder_.finish(reason); }

    // The reply to a diagnosticSession service request (request and reply
    // from byte 4 on, after header, code, sequence); the reply's length from
    // byte 4, 0 for none.
    std::size_t reply(Service service, const std::uint8_t* request, std::size_t length, std::uint8_t* out,
                      std::size_t capacity);
    static std::size_t replyThunk(void* context, Service service, const std::uint8_t* request, std::size_t length,
                                  std::uint8_t* out, std::size_t capacity) {
        return static_cast<DiagnosticSessions*>(context)->reply(service, request, length, out, capacity);
    }
    std::size_t totalBytes() const;
    std::size_t read(std::size_t offset, std::uint8_t* out, std::size_t count) const;

private:
    std::size_t header(std::uint8_t* out) const;

    slstm32::sensors::Recorder& recorder_;
    DiagnosticSession kind_;
    const slstm32::sensors::Quantity* quantities_{};
    std::uint32_t (*unixTime_)(){};
    std::uint32_t startUnix_{};
};

} // namespace b1s
