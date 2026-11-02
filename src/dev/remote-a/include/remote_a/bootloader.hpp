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

} // namespace remote_a::bootloader
