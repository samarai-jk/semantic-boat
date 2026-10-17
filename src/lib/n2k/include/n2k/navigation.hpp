#pragma once

#include <cstdint>
#include "n2k/bytes.hpp"

namespace n2k {

enum class HeadingReference : std::uint8_t { true_ = 0u, magnetic = 1u };

inline constexpr std::int32_t angleNotAvailable = 0x7FFFFFFF;

namespace detail {

// Thousandths of a degree to 1e-4 rad: m° * pi / 180000 * 1e4.
inline std::int32_t milliDegreesToRadians4(std::int32_t milliDegrees) {
    return static_cast<std::int32_t>(static_cast<std::int64_t>(milliDegrees) * 314159 / 1800000);
}

inline void putAngle16(std::uint8_t* out, std::int32_t milliDegrees, bool isSigned) {
    if (milliDegrees == angleNotAvailable) {
        bytes::put16(out, isSigned ? 0x7FFFu : 0xFFFFu);
        return;
    }
    bytes::put16(out, static_cast<std::uint16_t>(milliDegreesToRadians4(milliDegrees)));
}

} // namespace detail

// PGN 127250 Vessel Heading: SID, heading (0..2pi, 1e-4 rad), deviation and
// variation (not available here), reference. Angles in m°; angleNotAvailable
// for an unusable heading.
inline void encodeVesselHeading(std::uint8_t (&out)[8], std::uint8_t sequenceId, std::int32_t headingMilliDegrees,
                                HeadingReference reference) {
    out[0] = sequenceId;
    detail::putAngle16(out + 1, headingMilliDegrees, false);
    bytes::put16(out + 3, 0x7FFFu); // deviation
    bytes::put16(out + 5, 0x7FFFu); // variation
    out[7] = static_cast<std::uint8_t>(0xFCu | static_cast<std::uint8_t>(reference));
}

// PGN 127251 Rate of Turn: SID, rate (32 bit, 3.125e-8 rad/s), reserved.
// Rate in m°/s, positive turning to starboard.
inline void encodeRateOfTurn(std::uint8_t (&out)[8], std::uint8_t sequenceId, std::int32_t milliDegreesPerSecond) {
    out[0] = sequenceId;
    std::uint32_t bits = 0x7FFFFFFFu;
    if (milliDegreesPerSecond != angleNotAvailable) {
        // rad/s / 3.125e-8 = m°/s * pi / 180000 * 32e6
        bits = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(static_cast<std::int64_t>(milliDegreesPerSecond) * 3141593 * 32 / 180000));
    }
    bytes::put16(out + 1, static_cast<std::uint16_t>(bits));
    bytes::put16(out + 3, static_cast<std::uint16_t>(bits >> 16u));
    out[5] = out[6] = out[7] = 0xFFu;
}

// PGN 127257 Attitude: SID, yaw, pitch, roll (signed, 1e-4 rad), reserved.
// Angles in m°: pitch bow up positive, roll starboard down positive.
inline void encodeAttitude(std::uint8_t (&out)[8], std::uint8_t sequenceId, std::int32_t yaw, std::int32_t pitch,
                           std::int32_t roll) {
    out[0] = sequenceId;
    detail::putAngle16(out + 1, yaw, true);
    detail::putAngle16(out + 3, pitch, true);
    detail::putAngle16(out + 5, roll, true);
    out[7] = 0xFFu;
}

} // namespace n2k
