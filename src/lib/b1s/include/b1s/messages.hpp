#pragma once

#include <cstddef>
#include <cstdint>
#include "b1s/registry.hpp"
#include "n2k/bytes.hpp"
#include "n2k/name.hpp"

// Encoders of the B1S proprietary messages. Layouts and rules:
// src/lib/b1s/README.md, "B1S messages".
namespace b1s {

static_assert(manufacturerCode == n2k::manufacturerCode, "B1S registry and n2k disagree on the manufacturer code");

// Special int32 values of the measurement messages.
inline constexpr std::int32_t notAvailable = 0x7FFFFFFF; // no value yet, sensor missing
inline constexpr std::int32_t error = 0x7FFFFFFE;        // sensor fault
inline constexpr std::int32_t outOfRange = 0x7FFFFFFD;

// How a channel is sent (Channel Description flags, bits 0..1).
enum class Encoding : std::uint8_t {
    b1s = 0u,        // B1S message
    standard = 1u,   // standard NMEA 2000 PGN
    off = 2u,        // not sent
    noStandard = 3u, // B1S message; the quantity has no standard PGN
};

inline constexpr std::size_t maxTextLength = 32u;
inline constexpr std::size_t channelDescriptionCapacity = 8u + 2u * (2u + maxTextLength);

inline void putHeader(std::uint8_t* out) { n2k::bytes::put16(out, proprietaryHeader); }

// True if a proprietary message carries the B1S header (and not another
// manufacturer's).
inline bool isB1s(const std::uint8_t* data, std::size_t length) {
    return length >= 2u && data[0] == (proprietaryHeader & 0xFFu) && data[1] == (proprietaryHeader >> 8u);
}

// Measurement (PGN 65280): one value of one channel.
inline void encodeMeasurement(std::uint8_t (&out)[8], Quantity quantity, std::uint8_t channel, std::int32_t raw) {
    putHeader(out);
    out[2] = static_cast<std::uint8_t>(quantity);
    out[3] = channel;
    n2k::bytes::put32(out + 4, static_cast<std::uint32_t>(raw));
}

struct ChannelDescription {
    std::uint8_t channel{};
    std::uint8_t kind{}; // Quantity, or OutputKind for an output channel
    Encoding encoding{Encoding::b1s};
    bool output{};
    std::uint8_t standardInstance{0xFFu};    // 0xFF = none
    std::uint16_t periodCentiseconds{0xFFFFu}; // 0xFFFF = on change only
    const char* location{};                  // Signal K path fragment, "" = unassigned
    const char* label{};
};

// NMEA 2000 STRING_LAU (length including the two header bytes, 1 = ASCII),
// truncated to maxTextLength. Returns the bytes written.
inline std::size_t putStringLau(std::uint8_t* out, const char* text) {
    std::size_t length = 0u;
    while (text && text[length] != '\0' && length < maxTextLength) {
        out[2u + length] = static_cast<std::uint8_t>(text[length]);
        ++length;
    }
    out[0] = static_cast<std::uint8_t>(length + 2u);
    out[1] = 1u;
    return length + 2u;
}

// Channel Description (PGN 130819). Returns the message length.
inline std::size_t encodeChannelDescription(std::uint8_t (&out)[channelDescriptionCapacity],
                                            const ChannelDescription& description) {
    putHeader(out);
    out[2] = description.channel;
    out[3] = description.kind;
    out[4] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(description.encoding) |
                                       (description.output ? 0x04u : 0u) | 0xF8u);
    out[5] = description.standardInstance;
    n2k::bytes::put16(out + 6, description.periodCentiseconds);
    std::size_t length = 8u;
    length += putStringLau(out + length, description.location);
    length += putStringLau(out + length, description.label);
    return length;
}

} // namespace b1s
