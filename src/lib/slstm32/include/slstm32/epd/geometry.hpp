#pragma once

#include <cstdint>

namespace slstm32::epd {

struct Region {
    std::uint16_t x{};
    std::uint16_t y{};
    std::uint16_t width{};
    std::uint16_t height{};

    constexpr bool empty() const { return width == 0u || height == 0u; }
};

constexpr bool operator==(Region left, Region right) {
    return left.x == right.x && left.y == right.y &&
           left.width == right.width && left.height == right.height;
}

constexpr bool operator!=(Region left, Region right) { return !(left == right); }

constexpr Region unite(Region left, Region right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    const auto leftEnd = static_cast<std::uint32_t>(left.x) + left.width;
    const auto rightEnd = static_cast<std::uint32_t>(right.x) + right.width;
    const auto topEnd = static_cast<std::uint32_t>(left.y) + left.height;
    const auto bottomEnd = static_cast<std::uint32_t>(right.y) + right.height;
    const auto x = left.x < right.x ? left.x : right.x;
    const auto y = left.y < right.y ? left.y : right.y;
    const auto xEnd = leftEnd > rightEnd ? leftEnd : rightEnd;
    const auto yEnd = topEnd > bottomEnd ? topEnd : bottomEnd;
    return {x, y, static_cast<std::uint16_t>(xEnd - x),
            static_cast<std::uint16_t>(yEnd - y)};
}

} // namespace slstm32::epd
