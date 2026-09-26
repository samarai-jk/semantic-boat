#include "semantic_display/data.hpp"
#include <cmath>
#include <cstring>
#include <limits>

namespace semantic_display {

bool DataStore::configure(const PackageView& package) {
    if (!package.valid() || package.info().sourceCount > capacity_ || !slots_) return false;
    size_ = package.info().sourceCount;
    for (std::size_t index = 0; index < size_; ++index) slots_[index] = {};
    return true;
}

bool DataStore::setNumber(std::uint16_t sourceIndex, float value, std::uint32_t now) {
    if (sourceIndex >= size_) return false;
    slots_[sourceIndex] = {};
    slots_[sourceIndex].number = value;
    slots_[sourceIndex].updatedAt = now;
    slots_[sourceIndex].type = ValueType::number;
    return true;
}

bool DataStore::setText(std::uint16_t sourceIndex, std::string_view value, std::uint32_t now) {
    if (sourceIndex >= size_) return false;
    auto& slot = slots_[sourceIndex];
    slot = {};
    const auto length = value.size() < sizeof slot.text - 1u ? value.size() : sizeof slot.text - 1u;
    std::memcpy(slot.text, value.data(), length);
    slot.textLength = static_cast<std::uint8_t>(length);
    slot.updatedAt = now;
    slot.type = ValueType::text;
    return true;
}

bool DataStore::setUnavailable(std::uint16_t sourceIndex, std::uint32_t now) {
    if (sourceIndex >= size_) return false;
    slots_[sourceIndex] = {};
    slots_[sourceIndex].updatedAt = now;
    return true;
}

const ValueSlot* DataStore::get(std::uint16_t sourceIndex) const {
    return sourceIndex < size_ ? &slots_[sourceIndex] : nullptr;
}

std::uint16_t DataStore::findSource(const PackageView& package, std::string_view provider,
                                    std::string_view path) const {
    RecordView record{};
    if (!package.first(record)) return noIndex;
    do {
        SourceView source{};
        if (package.source(record, source) && source.provider == provider && source.path == path) {
            return source.sourceIndex;
        }
    } while (package.next(record));
    return noIndex;
}

float convertUnit(float value, std::string_view sourceUnit, std::string_view displayUnit) {
    if (displayUnit.empty() || sourceUnit == displayUnit) return value;
    if (sourceUnit == "m/s" && displayUnit == "kn") return value * 1.94384449f;
    if (sourceUnit == "rad" && displayUnit == "deg") return value * 57.2957795f;
    if (sourceUnit == "K" && (displayUnit == "C" || displayUnit == "degC")) return value - 273.15f;
    return value;
}

bool formatNumber(float value, std::uint8_t decimals, char* output, std::size_t capacity) {
    if (!output || capacity == 0u || decimals > 6u || !std::isfinite(value)) return false;
    std::int32_t scale = 1;
    for (std::uint8_t digit = 0u; digit < decimals; ++digit) scale *= 10;
    const auto scaledValue = static_cast<double>(value) * static_cast<double>(scale);
    constexpr auto maximum = static_cast<double>(std::numeric_limits<std::int64_t>::max());
    constexpr auto minimum = static_cast<double>(std::numeric_limits<std::int64_t>::min());
    if (scaledValue >= maximum || scaledValue <= minimum) return false;
    const auto scaled = static_cast<std::int64_t>(scaledValue +
        (scaledValue >= 0.0 ? 0.5 : -0.5));
    auto magnitude = scaled < 0
        ? static_cast<std::uint64_t>(-(scaled + 1)) + 1u
        : static_cast<std::uint64_t>(scaled);

    char reversed[24]{};
    std::size_t digitCount{};
    do {
        reversed[digitCount++] = static_cast<char>('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude != 0u);
    const auto displayedDigits = digitCount > decimals ? digitCount
        : static_cast<std::size_t>(decimals) + 1u;
    const auto required = (scaled < 0 ? 1u : 0u) + displayedDigits +
        (decimals != 0u ? 1u : 0u) + 1u;
    if (required > capacity) return false;

    std::size_t outputIndex{};
    if (scaled < 0) output[outputIndex++] = '-';
    for (auto remaining = displayedDigits; remaining != 0u; --remaining) {
        if (decimals != 0u && remaining == decimals) output[outputIndex++] = '.';
        output[outputIndex++] = remaining <= digitCount ? reversed[remaining - 1u] : '0';
    }
    output[outputIndex] = '\0';
    return true;
}

bool sourceIsSubscribed(const SourceView& source, std::uint16_t activeSection) {
    return source.permanent || source.sectionIndex == activeSection;
}

bool sourceIsSubscribed(const PackageView& package, std::uint16_t sourceIndex,
                        std::uint16_t activeSection) {
    SourceView source{};
    return package.sourceByIndex(sourceIndex, source) && sourceIsSubscribed(source, activeSection);
}

} // namespace semantic_display
