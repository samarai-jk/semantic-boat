#pragma once

#include <cstddef>
#include <cstdint>

// Little-endian field helpers for NMEA 2000 payloads.
namespace n2k::bytes {

inline void put16(std::uint8_t* out, std::uint16_t value) {
    out[0] = static_cast<std::uint8_t>(value);
    out[1] = static_cast<std::uint8_t>(value >> 8u);
}

inline void put24(std::uint8_t* out, std::uint32_t value) {
    out[0] = static_cast<std::uint8_t>(value);
    out[1] = static_cast<std::uint8_t>(value >> 8u);
    out[2] = static_cast<std::uint8_t>(value >> 16u);
}

inline void put64(std::uint8_t* out, std::uint64_t value) {
    for (std::size_t index = 0u; index < 8u; ++index) {
        out[index] = static_cast<std::uint8_t>(value >> (8u * index));
    }
}

inline std::uint32_t get24(const std::uint8_t* in) {
    return static_cast<std::uint32_t>(in[0]) | static_cast<std::uint32_t>(in[1]) << 8u |
           static_cast<std::uint32_t>(in[2]) << 16u;
}

inline std::uint64_t get64(const std::uint8_t* in) {
    std::uint64_t value = 0u;
    for (std::size_t index = 0u; index < 8u; ++index) {
        value |= static_cast<std::uint64_t>(in[index]) << (8u * index);
    }
    return value;
}

// Fixed-length string field, padded with 0xFF as unused bytes are "not
// available" in NMEA 2000.
inline void putString(std::uint8_t* out, std::size_t length, const char* text) {
    std::size_t index = 0u;
    for (; text && text[index] != '\0' && index < length; ++index) {
        out[index] = static_cast<std::uint8_t>(text[index]);
    }
    for (; index < length; ++index) out[index] = 0xFFu;
}

} // namespace n2k::bytes
