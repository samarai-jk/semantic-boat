#pragma once

#include <cstdint>

// Shared by the remote-a application and the remote-a bootloader
// (src/dev/remote-a-bootloader): the RTC backup register that hands the
// RS485 address over when the application starts the bootloader. Without it
// (after a power loss) the bootloader reads the address from the settings in
// the EEPROM. Registers 7..9 belong to the image protocol
// (b1s/firmware_image.hpp).
namespace remote_a::bootloader {

inline constexpr unsigned addressRegister = 10u;
inline constexpr std::uint32_t addressMagic = 0xADD10000u; // | address in bits 0..7

// The view before a firmware update (application only; the bootloader leaves
// them alone): register 11 holds the magic, the page index (bits 0..11) and
// the graph flag (bit 12), register 12 the configuration's source CRC, so the
// page is restored only for the same configuration.
inline constexpr unsigned viewRegister = 11u;
inline constexpr unsigned viewConfigurationRegister = 12u;
inline constexpr std::uint32_t viewMagic = 0x51E70000u;

} // namespace remote_a::bootloader
