#include "semantic_display/data.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    if (sourceUnit == "m" && displayUnit == "nm") return value / 1852.0f;
    if (sourceUnit == "Pa" && displayUnit == "hPa") return value / 100.0f;
    if (sourceUnit == "Pa" && displayUnit == "bar") return value / 100000.0f;
    if (sourceUnit == "Hz" && displayUnit == "rpm") return value * 60.0f;
    if (sourceUnit == "s" && displayUnit == "h") return value / 3600.0f;
    if (sourceUnit == "ratio" && displayUnit == "%") return value * 100.0f;
    if (sourceUnit == "m3/s" && displayUnit == "L/h") return value * 3600000.0f;
    return value;
}

ValueFreshness valueFreshness(const SourceView& source, const ValueSlot* value, std::uint32_t now) {
    if (!value || value->type == ValueType::unavailable) return ValueFreshness::unavailable;
    const auto age = static_cast<std::uint32_t>(now - value->updatedAt);
    if (source.staleAfterMs != 0u && age >= source.staleAfterMs) return ValueFreshness::stale;
    if (source.outdatedAfterMs != 0u && age >= source.outdatedAfterMs) return ValueFreshness::outdated;
    return ValueFreshness::fresh;
}

bool formatValueAge(std::uint32_t ageMs, char* output, std::size_t capacity) {
    if (!output || capacity == 0u) return false;
    const auto divisor = ageMs >= 3600000u ? 3600000u : ageMs >= 60000u ? 60000u : 1000u;
    const auto unit = ageMs >= 3600000u ? 'h' : ageMs >= 60000u ? 'm' : 's';
    const auto length = std::snprintf(output, capacity, "OLD %lu%c",
        static_cast<unsigned long>(ageMs / divisor), unit);
    return length >= 0 && static_cast<std::size_t>(length) < capacity;
}

bool formatPosition(std::string_view value, PositionFormat format, char* output, std::size_t capacity) {
    if (!output || capacity == 0u || value.empty() || value.size() >= 48u) return false;
    char input[48]{};
    std::memcpy(input, value.data(), value.size());
    char* latitudeEnd{};
    const auto latitude = std::strtod(input, &latitudeEnd);
    if (latitudeEnd == input || !std::isfinite(latitude) || latitude < 0.0 || latitude > 90.0 ||
        (*latitudeEnd != 'N' && *latitudeEnd != 'S') || latitudeEnd[1] != ' ') return false;
    const auto* longitudeStart = latitudeEnd + 2;
    char* longitudeEnd{};
    const auto longitude = std::strtod(longitudeStart, &longitudeEnd);
    if (longitudeEnd == longitudeStart || !std::isfinite(longitude) || longitude < 0.0 || longitude > 180.0 ||
        (*longitudeEnd != 'E' && *longitudeEnd != 'W') || longitudeEnd[1] != '\0') return false;
    const auto coordinate = [format](double magnitude, char hemisphere, char* text, std::size_t size) {
        const auto units = format == PositionFormat::dd ? 1000000u : format == PositionFormat::ddm ? 60000u : 36000u;
        const auto rounded = static_cast<unsigned long>(magnitude * units + 0.5);
        const auto degrees = rounded / units;
        const auto remainder = rounded % units;
        if (format == PositionFormat::dd) {
            return std::snprintf(text, size, "%s%lu.%06lu", hemisphere == 'S' || hemisphere == 'W' ? "-" : "", degrees, remainder);
        }
        if (format == PositionFormat::ddm) {
            return std::snprintf(text, size, "%lu\xc2\xb0%lu.%03lu'%c", degrees,
                                 remainder / 1000u, remainder % 1000u, hemisphere);
        }
        return std::snprintf(text, size, "%lu\xc2\xb0%lu'%lu.%01lu\"%c", degrees,
                     remainder / 600u, (remainder % 600u) / 10u, remainder % 10u, hemisphere);
    };
    if (format != PositionFormat::dd && format != PositionFormat::ddm && format != PositionFormat::dms) return false;
    char latitudeText[24]{}, longitudeText[24]{};
    const auto latitudeLength = coordinate(latitude, *latitudeEnd, latitudeText, sizeof latitudeText);
    const auto longitudeLength = coordinate(longitude, *longitudeEnd, longitudeText, sizeof longitudeText);
    if (latitudeLength < 0 || longitudeLength < 0 ||
        static_cast<std::size_t>(latitudeLength) >= sizeof latitudeText ||
        static_cast<std::size_t>(longitudeLength) >= sizeof longitudeText) return false;
    const auto length = std::snprintf(output, capacity, format == PositionFormat::dd ? "%s, %s" : "%s %s", latitudeText, longitudeText);
    return length >= 0 && static_cast<std::size_t>(length) < capacity;
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
