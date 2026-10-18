#pragma once

// Firmware build profile, set by sb_firmware_profile() in the firmware's
// CMakeLists.txt (cmake/firmware-profile.cmake). Rules: AGENTS.md, section
// "Firmware code categories and build profiles".
//
// Do not use these to switch development code on or off inside product
// sources: development code lives in its own files, which the build adds or
// leaves out. Use them only for markers and compile-time guards, for example:
//
//   if constexpr (slstm32::devTools) log.write(LogLevel::warning, "DEV BUILD");
//   static_assert(!slstm32::production || !simulatedInputs, "...");

#if !defined(SB_DEV_TOOLS) || !defined(SB_PRODUCTION)
#error "Firmware build profile missing: call sb_firmware_profile() for this target (see AGENTS.md)"
#endif

namespace slstm32 {

// Development and test tools are compiled into this firmware.
inline constexpr bool devTools = SB_DEV_TOOLS != 0;
// This firmware is a product (PRODUCTION profile), not a lab device.
inline constexpr bool production = SB_PRODUCTION != 0;

} // namespace slstm32
