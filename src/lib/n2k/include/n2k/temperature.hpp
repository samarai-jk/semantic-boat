#pragma once

#include <cstdint>
#include "n2k/bytes.hpp"
#include "n2k/can_id.hpp"

namespace n2k {

// NMEA 2000 temperature source lookup (subset).
enum class TemperatureSource : std::uint8_t {
    sea = 0u,
    outside = 1u,
    inside = 2u,
    engineRoom = 3u,
    mainCabin = 4u,
    refrigeration = 7u,
    heatingSystem = 8u,
    freezer = 13u,
    exhaustGas = 14u,
};

// PGN 130316 Temperature, Extended Range: SID, instance, source, actual
// temperature (24 bit, 0.001 K), set temperature (16 bit, 0.1 K, not set).
inline void encodeTemperatureExtendedRange(std::uint8_t (&out)[8], std::uint8_t sequenceId,
                                           std::uint8_t instance, TemperatureSource source,
                                           std::int32_t milliCelsius) {
    const std::int32_t milliKelvin = milliCelsius + 273150;
    out[0] = sequenceId;
    out[1] = instance;
    out[2] = static_cast<std::uint8_t>(source);
    bytes::put24(out + 3, milliKelvin > 0 ? static_cast<std::uint32_t>(milliKelvin) : 0u);
    bytes::put16(out + 6, 0xFFFFu);
}

// PGN 130316 with the actual temperature "not available" (sensor failed).
inline void encodeTemperatureNotAvailable(std::uint8_t (&out)[8], std::uint8_t sequenceId, std::uint8_t instance,
                                          TemperatureSource source) {
    out[0] = sequenceId;
    out[1] = instance;
    out[2] = static_cast<std::uint8_t>(source);
    bytes::put24(out + 3, 0xFFFFFFu);
    bytes::put16(out + 6, 0xFFFFu);
}

} // namespace n2k
