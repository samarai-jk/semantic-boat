#pragma once

#include <cstdint>
#include "n2k/bytes.hpp"

namespace n2k {

// NMEA 2000 pressure source lookup (subset).
enum class PressureSource : std::uint8_t {
    atmospheric = 0u,
    water = 1u,
    steam = 2u,
    compressedAir = 3u,
    hydraulic = 4u,
};

inline constexpr std::int32_t pressureNotAvailable = 0x7FFFFFFF;

// PGN 130314 Actual Pressure: SID, instance, source, pressure (32 bit
// signed, 0.1 Pa), reserved. pressureNotAvailable marks a failed sensor.
inline void encodeActualPressure(std::uint8_t (&out)[8], std::uint8_t sequenceId, std::uint8_t instance,
                                 PressureSource source, std::int32_t deciPascal) {
    out[0] = sequenceId;
    out[1] = instance;
    out[2] = static_cast<std::uint8_t>(source);
    const auto bits = static_cast<std::uint32_t>(deciPascal);
    bytes::put16(out + 3, static_cast<std::uint16_t>(bits));
    bytes::put16(out + 5, static_cast<std::uint16_t>(bits >> 16u));
    out[7] = 0xFFu;
}

} // namespace n2k
