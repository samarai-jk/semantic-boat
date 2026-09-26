#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include "semantic_display/package.hpp"

namespace semantic_display {

enum class ValueType : std::uint8_t { unavailable, number, text };

struct ValueSlot {
    float number{};
    std::uint32_t updatedAt{};
    ValueType type{ValueType::unavailable};
    std::uint8_t textLength{};
    char text[32]{};
};

class DataStore {
public:
    DataStore(ValueSlot* slots, std::size_t capacity) : slots_(slots), capacity_(capacity) {}

    bool configure(const PackageView& package);
    std::size_t size() const { return size_; }
    bool setNumber(std::uint16_t sourceIndex, float value, std::uint32_t now);
    bool setText(std::uint16_t sourceIndex, std::string_view value, std::uint32_t now);
    bool setUnavailable(std::uint16_t sourceIndex, std::uint32_t now);
    const ValueSlot* get(std::uint16_t sourceIndex) const;
    std::uint16_t findSource(const PackageView& package, std::string_view provider,
                             std::string_view path) const;

private:
    ValueSlot* slots_{};
    std::size_t capacity_{};
    std::size_t size_{};
};

float convertUnit(float value, std::string_view sourceUnit, std::string_view displayUnit);
bool formatNumber(float value, std::uint8_t decimals, char* output, std::size_t capacity);
bool sourceIsSubscribed(const SourceView& source, std::uint16_t activeSection);
bool sourceIsSubscribed(const PackageView& package, std::uint16_t sourceIndex,
                        std::uint16_t activeSection);

} // namespace semantic_display
