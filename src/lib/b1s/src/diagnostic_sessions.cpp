#include "b1s/diagnostic_sessions.hpp"
#include <cstring>
#include "b1s/measurement_publisher.hpp"
#include "b1s/messages.hpp"
#include "b1s/quantity.hpp"
#include "n2k/bytes.hpp"

namespace b1s {

namespace {

using slstm32::sensors::Recorder;

// Parameter status codes, reused for the session replies.
constexpr std::uint8_t done = 0u, invalidValue = 3u, busy = 5u;
constexpr std::uint8_t noSession = 0u, recording = 1u, complete = 2u;

void putFloat(std::uint8_t* out, float value) {
    std::uint32_t raw{};
    std::memcpy(&raw, &value, sizeof raw);
    n2k::bytes::put32(out, raw);
}

} // namespace

bool DiagnosticSessions::start() {
    if (!recorder_.start()) return false;
    startUnix_ = unixTime_ ? unixTime_() : 0u;
    return true;
}

bool DiagnosticSessions::phase(std::uint8_t code, const std::uint16_t* intervalsMs) {
    (void)recorder_.mark(0xFFu, static_cast<std::uint8_t>(EventKind::phase), code, 0);
    return recorder_.setPhase(intervalsMs);
}

void DiagnosticSessions::state(std::uint8_t channel, bool on, std::uint8_t role) {
    (void)recorder_.mark(channel, static_cast<std::uint8_t>(EventKind::state), role, on ? 1 : 0);
}

void DiagnosticSessions::snapshot(const ChannelConfig& channel) {
    const auto& measurement = *channel.measurement;
    std::int32_t value{};
    const auto raw = measurement.output(value) ? toRaw(measurement.quantity(), value) : notAvailable;
    (void)recorder_.mark(channel.channel, static_cast<std::uint8_t>(EventKind::snapshot),
                         static_cast<std::uint8_t>(quantityOf(measurement.quantity())), raw);
}

std::size_t DiagnosticSessions::header(std::uint8_t* out) const {
    const auto count = recorder_.seriesCount();
    out[0] = 'B'; out[1] = '1'; out[2] = 'D'; out[3] = 1u;
    out[4] = static_cast<std::uint8_t>(kind_);
    out[5] = recorder_.truncated() ? 1u : 0u;
    out[6] = static_cast<std::uint8_t>(recorder_.endReason());
    out[7] = static_cast<std::uint8_t>(count);
    n2k::bytes::put16(out + 8, recorder_.recordingNumber());
    n2k::bytes::put16(out + 10, static_cast<std::uint16_t>(recorder_.eventCount()));
    n2k::bytes::put32(out + 12, startUnix_);
    n2k::bytes::put32(out + 16, recorder_.durationMs());
    n2k::bytes::put32(out + 20, static_cast<std::uint32_t>(recorder_.sampleBytes()));
    auto* entry = out + headerBytes;
    for (std::size_t index = 0u; index < count; ++index, entry += seriesBytes) {
        const auto& series = recorder_.series(index);
        const auto quantity = quantities_[index];
        const auto code = quantityOf(quantity);
        const auto divisor = series.divisor != 0u ? series.divisor : 1u;
        // SI per stored unit: the B1S value of a large multiple of the unit
        // (precise for integer conversions such as rpm / 60), scaled.
        const auto zero = toRaw(quantity, 0);
        const std::int32_t multiple = 2000000000 / divisor < 1000000 ? 2000000000 / divisor : 1000000;
        const auto unit = static_cast<float>(static_cast<double>(toRaw(quantity, static_cast<std::int32_t>(divisor) * multiple) - zero) /
                                             static_cast<double>(multiple));
        entry[0] = series.id;
        entry[1] = static_cast<std::uint8_t>(code);
        n2k::bytes::put16(entry + 2, divisor);
        putFloat(entry + 4, unit * static_cast<float>(scale(code)));
        putFloat(entry + 8, static_cast<float>(zero) * static_cast<float>(scale(code)));
    }
    return headerBytes + count * seriesBytes;
}

std::size_t DiagnosticSessions::totalBytes() const {
    return headerBytes + recorder_.seriesCount() * seriesBytes + recorder_.eventCount() * Recorder::eventBytes +
           recorder_.sampleBytes();
}

std::size_t DiagnosticSessions::read(std::size_t offset, std::uint8_t* out, std::size_t count) const {
    std::uint8_t head[headerBytes + Recorder::maxSeries * seriesBytes];
    const auto headLength = header(head);
    const auto eventLength = recorder_.eventCount() * Recorder::eventBytes;
    const auto total = totalBytes();
    std::size_t copied{};
    for (; copied < count && offset + copied < total; ++copied) {
        const auto position = offset + copied;
        out[copied] = position < headLength ? head[position]
                    : position < headLength + eventLength ? recorder_.events()[position - headLength]
                                                          : recorder_.samples()[position - headLength - eventLength];
    }
    return copied;
}

std::size_t DiagnosticSessions::reply(Service service, const std::uint8_t* request, std::size_t length,
                                      std::uint8_t* out, std::size_t capacity) {
    const auto state = recorder_.state();
    const auto number = recorder_.recordingNumber();
    const auto requested = length >= 2u ? static_cast<std::uint16_t>(request[0] | request[1] << 8u) : 0u;
    switch (service) {
    case Service::diagnosticSessionInfo:
        // Status, number, kind, state, size, start, duration.
        if (capacity < 17u) return 0u;
        out[0] = done;
        n2k::bytes::put16(out + 1, number);
        out[3] = static_cast<std::uint8_t>(kind_);
        out[4] = number == 0u ? noSession : state == Recorder::State::recording ? recording
                                         : state == Recorder::State::complete  ? complete : noSession;
        n2k::bytes::put32(out + 5, out[4] == complete ? static_cast<std::uint32_t>(totalBytes()) : 0u);
        n2k::bytes::put32(out + 9, startUnix_);
        n2k::bytes::put32(out + 13, recorder_.durationMs());
        return 17u;
    case Service::diagnosticSessionRead: {
        // Request: number, offset (uint32), length. Reply: status, number, offset, count, bytes.
        if (capacity < 8u || length < 7u) return 0u;
        const auto offset = static_cast<std::size_t>(request[2] | request[3] << 8u | static_cast<std::uint32_t>(request[4]) << 16u |
                                                      static_cast<std::uint32_t>(request[5]) << 24u);
        auto count = static_cast<std::size_t>(request[6]);
        if (count > maxReadBytes) count = maxReadBytes;
        if (count > capacity - 8u) count = capacity - 8u;
        n2k::bytes::put16(out + 1, requested);
        n2k::bytes::put32(out + 3, static_cast<std::uint32_t>(offset));
        if (state != Recorder::State::complete || requested != number || offset > totalBytes()) {
            out[0] = state == Recorder::State::recording ? busy : invalidValue;
            out[7] = 0u;
            return 8u;
        }
        out[0] = done;
        out[7] = static_cast<std::uint8_t>(read(offset, out + 8, count));
        return 8u + out[7];
    }
    case Service::diagnosticSessionRelease:
        if (capacity < 3u) return 0u;
        if (state == Recorder::State::complete && requested == number) {
            recorder_.release();
            out[0] = done;
        } else {
            out[0] = invalidValue;
        }
        n2k::bytes::put16(out + 1, requested);
        return 3u;
    default:
        return 0u;
    }
}

} // namespace b1s
