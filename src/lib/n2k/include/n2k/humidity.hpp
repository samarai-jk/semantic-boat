#pragma once

#include <cstdint>
#include "n2k/bytes.hpp"

namespace n2k {

// NMEA 2000 humidity source lookup.
enum class HumiditySource : std::uint8_t {
    inside = 0u,
    outside = 1u,
};

inline constexpr std::int32_t humidityNotAvailable = 0x7FFFFFFF;

// PGN 130313 Humidity: SID, instance, source, actual humidity (16 bit,
// 0.004 %), set humidity (16 bit, not set), reserved. Humidity in 0.001 %;
// humidityNotAvailable marks a failed sensor.
inline void encodeHumidity(std::uint8_t (&out)[8], std::uint8_t sequenceId, std::uint8_t instance,
                           HumiditySource source, std::int32_t milliPercent) {
    out[0] = sequenceId;
    out[1] = instance;
    out[2] = static_cast<std::uint8_t>(source);
    std::uint16_t actual = 0x7FFFu;
    if (milliPercent != humidityNotAvailable) {
        auto units = (milliPercent + 2) / 4;
        if (units < -32000) units = -32000;
        if (units > 32000) units = 32000;
        actual = static_cast<std::uint16_t>(static_cast<std::int16_t>(units));
    }
    bytes::put16(out + 3, actual);
    bytes::put16(out + 5, 0x7FFFu);
    out[7] = 0xFFu;
}

} // namespace n2k
