#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/epd/geometry.hpp"

namespace slstm32::epd {

enum class Rotation : std::uint8_t { degrees0, degrees90, degrees180, degrees270 };

class MonochromeCanvas {
public:
    MonochromeCanvas(std::uint8_t* data, std::size_t size, std::uint16_t rawWidth,
                     std::uint16_t rawHeight, Rotation rotation = Rotation::degrees0);
    std::uint16_t width() const;
    std::uint16_t height() const;
    std::uint8_t* data() { return data_; }
    const std::uint8_t* data() const { return data_; }
    std::size_t size() const { return size_; }
    Region toRawRegion(Region logical) const;

    void clear(bool black = false);
    void setPixel(std::uint16_t x, std::uint16_t y, bool black = true);
    void fillRect(std::uint16_t x, std::uint16_t y, std::uint16_t width,
                  std::uint16_t height, bool black = true);
    void drawRect(std::uint16_t x, std::uint16_t y, std::uint16_t width,
                  std::uint16_t height, bool black = true, std::uint16_t thickness = 1u);

private:
    std::uint8_t* data_{};
    std::size_t size_{};
    std::uint16_t rawWidth_{};
    std::uint16_t rawHeight_{};
    Rotation rotation_{};
};

} // namespace slstm32::epd
