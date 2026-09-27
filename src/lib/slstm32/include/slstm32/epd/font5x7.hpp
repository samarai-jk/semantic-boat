#pragma once

#include <cstdint>
#include "slstm32/epd/monochrome_canvas.hpp"

namespace slstm32::epd {

class Font5x7 {
public:
    static void drawChar(MonochromeCanvas& canvas, std::uint16_t x, std::uint16_t y,
                         char character, bool black = true, std::uint8_t scale = 1u);
    static void drawText(MonochromeCanvas& canvas, std::uint16_t x, std::uint16_t y,
                         const char* text, bool black = true, std::uint8_t scale = 1u);
    static std::uint16_t textWidth(const char* text, std::uint8_t scale = 1u);
    static void drawTextScaled(MonochromeCanvas& canvas, std::uint16_t x,
                               std::uint16_t y, const char* text, bool black,
                               std::uint8_t numerator, std::uint8_t denominator);
    static std::uint16_t textWidthScaled(const char* text, std::uint8_t numerator,
                                         std::uint8_t denominator);
    static void drawCentered(MonochromeCanvas& canvas, std::uint16_t y, const char* text,
                             bool black = true, std::uint8_t scale = 1u);
};

} // namespace slstm32::epd
