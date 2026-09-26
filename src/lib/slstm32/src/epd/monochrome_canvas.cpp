#include "slstm32/epd/monochrome_canvas.hpp"
#include <cstring>

namespace slstm32::epd {

MonochromeCanvas::MonochromeCanvas(std::uint8_t* data, std::size_t size,
                                   std::uint16_t rawWidth, std::uint16_t rawHeight,
                                   Rotation rotation)
    : data_(data), size_(size), rawWidth_(rawWidth), rawHeight_(rawHeight), rotation_(rotation) {}

std::uint16_t MonochromeCanvas::width() const {
    return rotation_ == Rotation::degrees90 || rotation_ == Rotation::degrees270 ? rawHeight_ : rawWidth_;
}

std::uint16_t MonochromeCanvas::height() const {
    return rotation_ == Rotation::degrees90 || rotation_ == Rotation::degrees270 ? rawWidth_ : rawHeight_;
}

Region MonochromeCanvas::toRawRegion(Region logical) const {
    if (logical.x >= width() || logical.y >= height() || logical.empty()) return {};
    const auto logicalRight = static_cast<std::uint32_t>(logical.x) + logical.width;
    const auto logicalBottom = static_cast<std::uint32_t>(logical.y) + logical.height;
    if (logicalRight > width()) logical.width = static_cast<std::uint16_t>(width() - logical.x);
    if (logicalBottom > height()) logical.height = static_cast<std::uint16_t>(height() - logical.y);

    switch (rotation_) {
    case Rotation::degrees90:
        return {logical.y,
                static_cast<std::uint16_t>(rawHeight_ - logical.x - logical.width),
                logical.height, logical.width};
    case Rotation::degrees180:
        return {static_cast<std::uint16_t>(rawWidth_ - logical.x - logical.width),
                static_cast<std::uint16_t>(rawHeight_ - logical.y - logical.height),
                logical.width, logical.height};
    case Rotation::degrees270:
        return {static_cast<std::uint16_t>(rawWidth_ - logical.y - logical.height),
                logical.x, logical.height, logical.width};
    default:
        return logical;
    }
}

void MonochromeCanvas::clear(bool black) {
    if (data_) std::memset(data_, black ? 0x00 : 0xff, size_);
}

void MonochromeCanvas::setPixel(std::uint16_t x, std::uint16_t y, bool black) {
    if (!data_ || x >= width() || y >= height()) return;
    std::uint16_t rawX{};
    std::uint16_t rawY{};
    switch (rotation_) {
    case Rotation::degrees90:  rawX = y; rawY = static_cast<std::uint16_t>(rawHeight_ - x - 1u); break;
    case Rotation::degrees180: rawX = static_cast<std::uint16_t>(rawWidth_ - x - 1u); rawY = static_cast<std::uint16_t>(rawHeight_ - y - 1u); break;
    case Rotation::degrees270: rawX = static_cast<std::uint16_t>(rawWidth_ - y - 1u); rawY = x; break;
    default: rawX = x; rawY = y; break;
    }
    const auto index = static_cast<std::size_t>(rawY) * ((rawWidth_ + 7u) / 8u) + rawX / 8u;
    if (index >= size_) return;
    const auto mask = static_cast<std::uint8_t>(0x80u >> (rawX & 7u));
    if (black) data_[index] &= static_cast<std::uint8_t>(~mask);
    else data_[index] |= mask;
}

void MonochromeCanvas::fillRect(std::uint16_t x, std::uint16_t y, std::uint16_t rectWidth,
                                std::uint16_t rectHeight, bool black) {
    const auto xEnd = static_cast<std::uint16_t>(x + rectWidth > width() ? width() : x + rectWidth);
    const auto yEnd = static_cast<std::uint16_t>(y + rectHeight > height() ? height() : y + rectHeight);
    for (auto py = y; py < yEnd; ++py) for (auto px = x; px < xEnd; ++px) setPixel(px, py, black);
}

void MonochromeCanvas::drawRect(std::uint16_t x, std::uint16_t y, std::uint16_t rectWidth,
                                std::uint16_t rectHeight, bool black, std::uint16_t thickness) {
    if (rectWidth < thickness * 2u || rectHeight < thickness * 2u) return;
    fillRect(x, y, rectWidth, thickness, black);
    fillRect(x, static_cast<std::uint16_t>(y + rectHeight - thickness), rectWidth, thickness, black);
    fillRect(x, y, thickness, rectHeight, black);
    fillRect(static_cast<std::uint16_t>(x + rectWidth - thickness), y, thickness, rectHeight, black);
}

} // namespace slstm32::epd
