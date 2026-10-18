#pragma once

#include <cstdint>
#include "n2k/bytes.hpp"

namespace n2k {

inline constexpr std::int32_t engineSpeedNotAvailable = -1;

// PGN 127488 Engine Parameters, Rapid Update: engine instance, speed
// (16 bit, 0.25 rpm), boost pressure and tilt/trim (not available here),
// reserved. Speed in thousandths of an rpm; engineSpeedNotAvailable for a
// failed sensor.
inline void encodeEngineRapidUpdate(std::uint8_t (&out)[8], std::uint8_t instance, std::int32_t milliRpm) {
    out[0] = instance;
    std::uint16_t speed = 0xFFFFu;
    if (milliRpm >= 0) {
        const auto quarters = static_cast<std::uint32_t>(milliRpm) / 250u;
        speed = static_cast<std::uint16_t>(quarters > 0xFFFDu ? 0xFFFDu : quarters);
    }
    bytes::put16(out + 1, speed);
    bytes::put16(out + 3, 0xFFFFu); // boost pressure
    out[5] = 0x7Fu;                 // tilt/trim
    out[6] = out[7] = 0xFFu;
}

} // namespace n2k
