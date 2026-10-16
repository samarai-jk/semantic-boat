#pragma once

#include <cstddef>
#include <cstdint>
#include "n2k/bytes.hpp"
#include "n2k/can_id.hpp"

namespace n2k {

// PGN 127508 Battery Status: instance, voltage (0.01 V), current (0.1 A, not
// available), temperature (0.01 K, not available), SID.
inline void encodeBatteryStatus(std::uint8_t (&out)[8], std::uint8_t instance,
                                std::int32_t milliVolts, std::uint8_t sequenceId = noSequenceId) {
    const std::int32_t centiVolts = (milliVolts >= 0 ? milliVolts + 5 : milliVolts - 5) / 10;
    out[0] = instance;
    bytes::put16(out + 1, static_cast<std::uint16_t>(static_cast<std::int16_t>(centiVolts)));
    bytes::put16(out + 3, 0x7FFFu);
    bytes::put16(out + 5, 0xFFFFu);
    out[7] = sequenceId;
}

// PGN 127501 Binary Switch Bank Status: bank instance and up to 28 two-bit
// indicator states (0 off, 1 on, 3 not available).
inline void encodeBinarySwitchBankStatus(std::uint8_t (&out)[8], std::uint8_t instance,
                                         const bool* states, std::size_t count) {
    out[0] = instance;
    for (std::size_t index = 1u; index < 8u; ++index) out[index] = 0xFFu;
    for (std::size_t index = 0u; index < count && index < 28u; ++index) {
        const auto byte = 1u + index / 4u;
        const auto shift = static_cast<unsigned>((index % 4u) * 2u);
        out[byte] = static_cast<std::uint8_t>((out[byte] & ~(3u << shift)) | ((states[index] ? 1u : 0u) << shift));
    }
}

} // namespace n2k
