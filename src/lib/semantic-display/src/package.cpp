#include "semantic_display/package.hpp"
#include <cmath>
#include <cstring>

namespace semantic_display {
namespace {

std::uint16_t read16(const std::uint8_t* value) {
    return static_cast<std::uint16_t>(value[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(value[1]) << 8u);
}

std::uint32_t read32(const std::uint8_t* value) {
    return static_cast<std::uint32_t>(value[0]) |
           (static_cast<std::uint32_t>(value[1]) << 8u) |
           (static_cast<std::uint32_t>(value[2]) << 16u) |
           (static_cast<std::uint32_t>(value[3]) << 24u);
}

bool takeString(const std::uint8_t*& current, const std::uint8_t* end,
                std::uint8_t length, std::string_view& result) {
    if (static_cast<std::size_t>(end - current) < length) return false;
    result = {reinterpret_cast<const char*>(current), length};
    current += length;
    return true;
}

} // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

PackageInfo PackageView::info() const {
    PackageInfo result{};
    if (!data_ || size_ < packageHeaderSize) return result;
    result.totalSize = read32(data_ + 8u);
    result.payloadCrc32 = read32(data_ + 12u);
    result.sourceCount = read16(data_ + 16u);
    result.sectionCount = read16(data_ + 18u);
    result.pageCount = read16(data_ + 20u);
    result.widgetCount = read16(data_ + 22u);
    result.historyBytes = read32(data_ + 24u);
    result.sourceCrc32 = read32(data_ + 28u);
    return result;
}

bool PackageView::headerValid() const {
    if (!data_ || size_ < packageHeaderSize || read32(data_) != packageMagic ||
        read16(data_ + 4u) != packageFormatVersion ||
        read16(data_ + 6u) != packageHeaderSize) return false;
    const auto details = info();
    return details.totalSize >= packageHeaderSize && details.totalSize <= size_;
}

bool PackageView::valid() const {
    if (!headerValid()) return false;
    const auto details = info();
    return crc32(data_ + packageHeaderSize, details.totalSize - packageHeaderSize) ==
           details.payloadCrc32;
}

bool PackageView::recordAt(std::size_t offset, RecordView& record) const {
    if (!headerValid() || offset > info().totalSize || info().totalSize - offset < 4u) return false;
    const auto recordSize = read16(data_ + offset + 2u);
    if (recordSize < 4u || offset + recordSize > info().totalSize) return false;
    record.type = static_cast<RecordType>(data_[offset]);
    record.version = data_[offset + 1u];
    record.payload = data_ + offset + 4u;
    record.payloadSize = static_cast<std::uint16_t>(recordSize - 4u);
    record.offset = offset;
    record.nextOffset = offset + recordSize;
    return true;
}

bool PackageView::first(RecordView& record) const {
    return valid() && info().totalSize > packageHeaderSize && recordAt(packageHeaderSize, record);
}

bool PackageView::next(RecordView& record) const {
    if (!headerValid() || record.nextOffset >= info().totalSize) return false;
    return recordAt(record.nextOffset, record);
}

bool PackageView::source(const RecordView& record, SourceView& sourceResult) const {
    if (record.type != RecordType::source || record.payloadSize < 24u) return false;
    sourceResult = {};
    const auto* current = record.payload;
    const auto* end = record.payload + record.payloadSize;
    sourceResult.sectionIndex = read16(current); current += 2u;
    sourceResult.sourceIndex = read16(current); current += 2u;
    sourceResult.subscribePeriodMs = read32(current); current += 4u;
    sourceResult.staleAfterMs = read32(current); current += 4u;
    sourceResult.historyIntervalMs = read32(current); current += 4u;
    sourceResult.historyPoints = read16(current); current += 2u;
    sourceResult.historyReducer = static_cast<HistoryReducer>(*current++);
    sourceResult.permanent = (*current++ & 1u) != 0u;
    const auto providerLength = *current++;
    const auto idLength = *current++;
    const auto pathLength = *current++;
    const auto unitLength = *current++;
        if (!(takeString(current, end, providerLength, sourceResult.provider) &&
           takeString(current, end, idLength, sourceResult.id) &&
           takeString(current, end, pathLength, sourceResult.path) &&
            takeString(current, end, unitLength, sourceResult.unit))) return false;
        if (current == end) return true;
        if (end - current != 4) return false;
        sourceResult.outdatedAfterMs = read32(current);
        return sourceResult.staleAfterMs == 0u || sourceResult.outdatedAfterMs < sourceResult.staleAfterMs;
}

bool PackageView::section(const RecordView& record, SectionView& result) const {
    if (record.type != RecordType::section || record.payloadSize < 4u) return false;
    result = {};
    const auto* current = record.payload;
    const auto* end = record.payload + record.payloadSize;
    result.sectionIndex = read16(current); current += 2u;
    const auto idLength = *current++;
    const auto titleLength = *current++;
    if (!takeString(current, end, idLength, result.id) ||
        !takeString(current, end, titleLength, result.title)) return false;

    // Early v1 packages ended after title. Keep them readable when firmware is
    // upgraded while an older compiled package remains in EEPROM.
    if (current == end) return true;
    if (static_cast<std::size_t>(end - current) < 5u) return false;
    result.showButtonLabels = (*current++ & 1u) != 0u;
    const auto previousLength = *current++;
    const auto nextLength = *current++;
    const auto action1Length = *current++;
    const auto action2Length = *current++;
    return takeString(current, end, previousLength, result.previousPageLabel) &&
           takeString(current, end, nextLength, result.nextPageLabel) &&
           takeString(current, end, action1Length, result.action1Label) &&
           takeString(current, end, action2Length, result.action2Label) && current == end;
}

bool PackageView::page(const RecordView& record, PageView& result) const {
    if (record.type != RecordType::page || record.payloadSize < 9u) return false;
    const auto* current = record.payload;
    const auto* end = record.payload + record.payloadSize;
    result.sectionIndex = read16(current); current += 2u;
    result.pageIndex = read16(current); current += 2u;
    result.columns = *current++;
    result.rows = *current++;
    result.gap = *current++;
    const auto idLength = *current++;
    const auto titleLength = *current++;
    return takeString(current, end, idLength, result.id) &&
           takeString(current, end, titleLength, result.title) && current == end;
}

bool PackageView::widget(const RecordView& record, WidgetView& result) const {
    if (record.type != RecordType::widget || record.payloadSize < 18u) return false;
    result = {};
    const auto* current = record.payload;
    const auto* end = record.payload + record.payloadSize;
    result.sectionIndex = read16(current); current += 2u;
    result.pageIndex = read16(current); current += 2u;
    result.sourceIndex = read16(current); current += 2u;
    result.type = static_cast<WidgetType>(*current++);
    result.column = *current++;
    result.row = *current++;
    result.columnSpan = *current++;
    result.rowSpan = *current++;
    result.decimals = static_cast<std::int8_t>(*current++);
    const auto typeLength = *current++;
    const auto sourceLength = *current++;
    const auto labelLength = *current++;
    const auto unitLength = *current++;
    const auto textLength = *current++;
    const auto storedMaxDigits = *current++;
    result.maxDigits = storedMaxDigits != 0u ? storedMaxDigits : defaultValueMaxDigits;
        if (!(takeString(current, end, typeLength, result.typeName) &&
           takeString(current, end, sourceLength, result.sourceId) &&
           takeString(current, end, labelLength, result.label) &&
           takeString(current, end, unitLength, result.displayUnit) &&
            takeString(current, end, textLength, result.text))) return false;
        if (current == end) return true;
        if (end - current != 1 || *current > static_cast<std::uint8_t>(PositionFormat::dms)) return false;
        result.positionFormat = static_cast<PositionFormat>(*current);
        return true;
}

bool PackageView::historyPlot(const RecordView& record, HistoryPlotView& result) const {
    if (record.type != RecordType::historyPlot || record.payloadSize < 19u) return false;
    result = HistoryPlotView{};
    const auto* current = record.payload;
    const auto* end = current + record.payloadSize;
    result.sectionIndex = read16(current); current += 2u;
    result.pageIndex = read16(current); current += 2u;
    result.sourceIndex = read16(current); current += 2u;
    const auto flags = *current++;
    result.fixedRange = (flags & 1u) != 0u;
    result.decimals = *current++;
    const auto sourceLength = *current++;
    const auto titleLength = *current++;
    const auto unitLength = *current++;
    auto raw = read32(current); current += 4u;
    std::memcpy(&result.minimum, &raw, sizeof raw);
    raw = read32(current); current += 4u;
    std::memcpy(&result.maximum, &raw, sizeof raw);
    result.circular = (flags & 2u) != 0u;
    const auto rangeValid = [](bool fixed, float minimum, float maximum) {
        return !fixed || (minimum < maximum && std::isfinite(minimum) && std::isfinite(maximum) &&
                          std::isfinite(maximum - minimum));
    };
    if (flags > 3u || result.decimals > 6u || result.sourceIndex >= info().sourceCount ||
        result.pageIndex >= info().pageCount || result.sectionIndex >= info().sectionCount ||
        !rangeValid(result.fixedRange, result.minimum, result.maximum)) return false;
    if (!takeString(current, end, sourceLength, result.sourceId) ||
        !takeString(current, end, titleLength, result.title) ||
        !takeString(current, end, unitLength, result.displayUnit)) return false;
    if (current == end) return true;
    // Extra series: count, then per series index, flags, decimals, string
    // lengths, range, strings.
    result.extraCount = *current++;
    if (result.extraCount == 0u || result.extraCount > maxHistoryPlotExtraSeries) return false;
    for (std::uint8_t index = 0u; index < result.extraCount; ++index) {
        auto& series = result.extra[index];
        if (end - current < 15) return false;
        series.sourceIndex = read16(current);
        const auto seriesFlags = current[2];
        series.fixedRange = (seriesFlags & 1u) != 0u;
        series.circular = (seriesFlags & 2u) != 0u;
        series.decimals = current[3];
        const auto seriesSource = current[4], seriesTitle = current[5], seriesUnit = current[6];
        raw = read32(current + 7u);
        std::memcpy(&series.minimum, &raw, sizeof raw);
        raw = read32(current + 11u);
        std::memcpy(&series.maximum, &raw, sizeof raw);
        current += 15u;
        if (seriesFlags > 3u || series.decimals > 6u || series.sourceIndex >= info().sourceCount ||
            !rangeValid(series.fixedRange, series.minimum, series.maximum) ||
            !takeString(current, end, seriesSource, series.sourceId) ||
            !takeString(current, end, seriesTitle, series.title) ||
            !takeString(current, end, seriesUnit, series.displayUnit)) return false;
    }
    return current == end;
}

bool PackageView::historyPlotByPage(std::uint16_t index, HistoryPlotView& result) const {
    RecordView record{};
    if (!first(record)) return false;
    do {
        if (historyPlot(record, result) && result.pageIndex == index) return true;
    } while (next(record));
    return false;
}

bool PackageView::sourceByIndex(std::uint16_t index, SourceView& result) const {
    RecordView record{};
    if (!first(record)) return false;
    do {
        if (record.type == RecordType::source && source(record, result) &&
            result.sourceIndex == index) return true;
    } while (next(record));
    return false;
}

} // namespace semantic_display
