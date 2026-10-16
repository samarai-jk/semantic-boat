#pragma once

#include <cstdint>

namespace n2k {

inline constexpr std::uint8_t broadcastAddress = 255u;
inline constexpr std::uint8_t nullAddress = 254u; // "cannot claim"
inline constexpr std::uint8_t maximumAddress = 251u;
// SID value meaning "not tied to other messages".
inline constexpr std::uint8_t noSequenceId = 0xFFu;

// Fields of a 29-bit NMEA 2000 / J1939 CAN identifier. For PDU1 PGNs (PF below
// 240) the PGN is stored with its low byte cleared and the destination is
// carried separately; PDU2 PGNs are always broadcast.
struct CanId {
    std::uint8_t priority{6u};
    std::uint32_t pgn{};
    std::uint8_t source{};
    std::uint8_t destination{broadcastAddress};
};

constexpr bool isPdu1(std::uint32_t pgn) {
    return ((pgn >> 8u) & 0xFFu) < 240u;
}

constexpr std::uint32_t encodeCanId(const CanId& id) {
    auto pgnField = id.pgn & 0x3FFFFu;
    if (isPdu1(pgnField)) pgnField = (pgnField & 0x3FF00u) | id.destination;
    return (static_cast<std::uint32_t>(id.priority & 7u) << 26u) | (pgnField << 8u) | id.source;
}

constexpr CanId decodeCanId(std::uint32_t raw) {
    CanId id{};
    id.priority = static_cast<std::uint8_t>((raw >> 26u) & 7u);
    id.source = static_cast<std::uint8_t>(raw & 0xFFu);
    const auto pgnField = (raw >> 8u) & 0x3FFFFu;
    if (isPdu1(pgnField)) {
        id.pgn = pgnField & 0x3FF00u;
        id.destination = static_cast<std::uint8_t>(pgnField & 0xFFu);
    } else {
        id.pgn = pgnField;
        id.destination = broadcastAddress;
    }
    return id;
}

} // namespace n2k
