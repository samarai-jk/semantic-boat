#pragma once

#include <cstdint>

// Bootloader version, reported in the firmware status.
inline constexpr char bootloaderVersion[] = "1.0.0";
inline constexpr std::uint32_t bootloaderVersionNumber = 1u << 24u; // major << 24 | minor << 16 | patch << 8
