// Host tests of the slstm32 Recorder and B1S diagnostic sessions. Build and
// run from the repository root:
//   g++ -std=c++17 -Wall -Wextra -Isrc/lib/b1s/include -Isrc/lib/n2k/include -Isrc/lib/slstm32/include
//       src/lib/b1s/tests/diagnostics_tests.cpp src/lib/b1s/src/diagnostic_sessions.cpp src/lib/b1s/src/quantity.cpp
//       src/lib/slstm32/src/sensors/{recorder,measurement,window}.cpp -o build/diagnostics_tests
//       && build/diagnostics_tests [session.bin]
// With a file name it writes the session's byte stream (the hub's decoder
// test uses it as fixture).
#include <cassert>
#include <cstdio>
#include <cstring>
#include "b1s/diagnostic_sessions.hpp"
#include "b1s/measurement_publisher.hpp"

namespace {

std::uint32_t now = 5000u;
std::uint32_t millis() { return now; }
std::uint32_t unixTime() { return 1794870000u; }
std::int32_t voltage = 13280; // mV
std::int32_t rpm = 0;          // 0.001 rpm
bool readVoltage(void*, std::int32_t& value) { value = voltage; return true; }
bool readRpm(void*, std::int32_t& value) { value = rpm; return true; }
bool readMissing(void*, std::int32_t&) { return false; }

std::uint32_t get32(const std::uint8_t* in) {
    return static_cast<std::uint32_t>(in[0] | in[1] << 8u | in[2] << 16u) | static_cast<std::uint32_t>(in[3]) << 24u;
}
std::int16_t get16(const std::uint8_t* in) { return static_cast<std::int16_t>(in[0] | in[1] << 8u); }

void advance(slstm32::sensors::Recorder& recorder, std::uint32_t ms, std::uint32_t step = 1u) {
    for (std::uint32_t passed = 0u; passed < ms; passed += step) { now += step; recorder.run(); }
}

} // namespace

int main(int argc, char** argv) {
    using slstm32::sensors::Recorder;
    namespace sensors = slstm32::sensors;
    const slstm32::Runtime runtime{&millis, nullptr, nullptr};
    const sensors::RecorderSeries series[]{{13u, 1u, &readVoltage, nullptr}, {2u, 1000u, &readRpm, nullptr},
                                           {9u, 1u, &readMissing, nullptr}};
    const sensors::Quantity quantities[]{sensors::Quantity::voltage, sensors::Quantity::rotationalSpeed,
                                         sensors::Quantity::voltage};
    static std::uint8_t arena[2048];
    static std::uint8_t leadIn[2u * 3u * 5u]; // 5 frames
    Recorder recorder{runtime, series, 3u, arena, sizeof arena, 8u, leadIn, sizeof leadIn, 100u};
    b1s::DiagnosticSessions sessions{recorder, b1s::DiagnosticSession::engineStart, quantities, &unixTime};

    // Idle: the lead-in ring keeps the last 5 frames (0.5 s).
    advance(recorder, 1000u);
    assert(recorder.state() == Recorder::State::idle);
    assert(sessions.start());
    sensors::MeasurementConfig hoursConfig{"hours", sensors::Quantity::runTime, 0u};
    sensors::Measurement hours{hoursConfig};
    hours.update(607100, now);
    const b1s::ChannelConfig hoursChannel{&hours, 21u};
    sessions.snapshot(hoursChannel);
    sessions.state(22u, true, 1u);
    const std::uint16_t waiting[]{100u, 100u, 100u};
    const std::uint16_t cranking[]{10u, 50u, 0u};
    assert(sessions.phase(1u, waiting));
    advance(recorder, 1000u);           // ticks at 0, 100 .. 1000 ms
    assert(sessions.phase(3u, cranking));
    voltage = 10500; rpm = 250000;
    advance(recorder, 500u);            // 51 ticks: voltage every tick, rpm every 5th
    sessions.state(23u, false, 2u);
    sessions.finish(Recorder::EndReason::finished);
    assert(recorder.state() == Recorder::State::complete);

    // The byte stream: header, series, events, samples.
    std::uint8_t data[4096]{};
    const auto total = sessions.totalBytes();
    assert(total < sizeof data);
    assert(sessions.read(0u, data, total) == total);
    assert(std::memcmp(data, "B1D\x01", 4) == 0 && data[4] == 1u && data[7] == 3u);
    assert(get32(data + 12) == 1794870000u && get32(data + 16) == 1500u);
    const auto events = static_cast<std::size_t>(data[10] | data[11] << 8u);
    assert(events == 5u); // snapshot, state, phase, phase, state
    // Series 2 (rpm): 1 rpm per stored unit = 1/60 Hz.
    float rpmScale{};
    std::memcpy(&rpmScale, data + 24 + 12 + 4, 4);
    assert(data[24 + 12] == 2u && data[24 + 12 + 1] == 9u && rpmScale > 0.01666f && rpmScale < 0.01667f);
    // Snapshot of the engine hours as its B1S value (seconds).
    const auto* event = data + 24 + 3 * 12;
    assert(event[4] == 21u && event[5] == 1u && event[6] == 25u && get32(event + 8) == 2185560u);
    // Segments: lead-in (5 frames before the start), waiting (10), cranking (50).
    const auto* segment = data + 24 + 3 * 12 + events * 12;
    assert(static_cast<std::int32_t>(get32(segment)) == -500 && get16(segment + 6) == 5);
    assert(get16(segment + 8 + 3) == 13280 && get16(segment + 8 + 3 + 4) == Recorder::missing);
    segment += 8 + 3 + 5 * 6;
    assert(get32(segment) == 0u && get16(segment + 4) == 100 && get16(segment + 6) == 11); // ticks 0..1000 ms
    segment += 8 + 3 + 11 * 6;
    assert(get32(segment) == 1000u && get16(segment + 4) == 10 && get16(segment + 6) == 51);
    assert(segment[8] == 1u && segment[9] == 5u && segment[10] == 0u);
    // Tick 0: voltage and rpm; tick 1: voltage only.
    assert(get16(segment + 11) == 10500 && get16(segment + 13) == 250 && get16(segment + 15) == 10500);
    assert(segment + 11 + 51 * 2 + 11 * 2 == data + total);

    // Service replies: info, read in pieces, release.
    std::uint8_t reply[220]{};
    assert(sessions.reply(b1s::Service::diagnosticSessionInfo, nullptr, 0u, reply, sizeof reply) == 17u);
    assert(reply[0] == 0u && reply[3] == 1u && reply[4] == 2u && get32(reply + 5) == total);
    const std::uint8_t readRequest[]{1u, 0u, 10u, 0u, 0u, 0u, 200u};
    const auto length = sessions.reply(b1s::Service::diagnosticSessionRead, readRequest, sizeof readRequest, reply, sizeof reply);
    assert(length == 8u + 200u && reply[7] == 200u && std::memcmp(reply + 8, data + 10, 200u) == 0);
    const std::uint8_t wrong[]{7u, 0u, 0u, 0u, 0u, 0u, 10u};
    assert(sessions.reply(b1s::Service::diagnosticSessionRead, wrong, sizeof wrong, reply, sizeof reply) == 8u && reply[0] == 3u);
    if (argc > 1) {
        auto* file = std::fopen(argv[1], "wb");
        assert(file);
        std::fwrite(data, 1u, total, file);
        std::fclose(file);
    }
    const std::uint8_t release[]{1u, 0u};
    assert(sessions.reply(b1s::Service::diagnosticSessionRelease, release, sizeof release, reply, sizeof reply) == 3u);
    assert(reply[0] == 0u && recorder.state() == Recorder::State::idle);

    // A full arena ends the recording, truncated.
    assert(sessions.start());
    const std::uint16_t fast[]{10u, 10u, 10u};
    assert(sessions.phase(3u, fast));
    advance(recorder, 10000u);
    assert(recorder.state() == Recorder::State::complete && recorder.truncated() &&
           recorder.endReason() == Recorder::EndReason::full);
    std::puts("diagnostics tests passed");
    return 0;
}
