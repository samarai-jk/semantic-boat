#pragma once

#include "b1s/firmware_image.hpp"

// Firmware images of can-mcu devices: the B1S image format and layout
// (b1s/firmware_image.hpp) with the can-mcu board.
namespace can_mcu::image {

using namespace b1s::image;

inline constexpr std::uint16_t boardCanMcuV1 = static_cast<std::uint16_t>(b1s::Board::canMcuV1);

// The header an application defines in section .image_header.
constexpr Header makeHeader(std::uint16_t productCode, std::uint8_t major, std::uint8_t minor, std::uint8_t patch,
                            bool development, const char* text) {
    return b1s::image::makeHeader(b1s::Board::canMcuV1, productCode, major, minor, patch, development, text);
}

} // namespace can_mcu::image
