#pragma once

#include <cstddef>
#include <cstdint>

namespace n2k {

// ISO 11783 / NMEA 2000 NAME. A lower 64-bit value wins address contention.
struct Name {
    std::uint32_t uniqueNumber{};       // 21 bits, unique per manufacturer
    std::uint16_t manufacturerCode{};   // 11 bits, assigned by NMEA
    std::uint8_t deviceInstanceLower{}; // 3 bits
    std::uint8_t deviceInstanceUpper{}; // 5 bits
    std::uint8_t deviceFunction{};      // 8 bits, meaning depends on the class
    std::uint8_t deviceClass{};         // 7 bits
    std::uint8_t systemInstance{};      // 4 bits
    std::uint8_t industryGroup{4u};     // 3 bits, 4 = marine
    bool arbitraryAddressCapable{true};

    constexpr std::uint64_t value() const {
        return (static_cast<std::uint64_t>(uniqueNumber & 0x1FFFFFu)) |
               (static_cast<std::uint64_t>(manufacturerCode & 0x7FFu) << 21u) |
               (static_cast<std::uint64_t>(deviceInstanceLower & 0x7u) << 32u) |
               (static_cast<std::uint64_t>(deviceInstanceUpper & 0x1Fu) << 35u) |
               (static_cast<std::uint64_t>(deviceFunction) << 40u) |
               (static_cast<std::uint64_t>(deviceClass & 0x7Fu) << 49u) |
               (static_cast<std::uint64_t>(systemInstance & 0xFu) << 56u) |
               (static_cast<std::uint64_t>(industryGroup & 0x7u) << 60u) |
               (static_cast<std::uint64_t>(arbitraryAddressCapable ? 1u : 0u) << 63u);
    }
};

// Folds a device serial (for example an MCU unique ID) into the 21-bit NAME
// unique number with FNV-1a.
constexpr std::uint32_t uniqueNumberFrom(const std::uint8_t* serial, std::size_t size) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t index = 0u; index < size; ++index) {
        hash ^= serial[index];
        hash *= 16777619u;
    }
    return (hash ^ (hash >> 21u)) & 0x1FFFFFu;
}

} // namespace n2k
