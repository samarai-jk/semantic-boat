#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace semantic_display {

inline constexpr std::uint32_t packageMagic = 0x31444253u; // "SBD1"
inline constexpr std::uint16_t packageFormatVersion = 1u;
inline constexpr std::size_t packageHeaderSize = 32u;
inline constexpr std::uint16_t noIndex = 0xffffu;
inline constexpr std::size_t historySeriesOverheadBytes = 32u;

enum class RecordType : std::uint8_t { source = 1u, section = 2u, page = 3u, widget = 4u };
enum class WidgetType : std::uint8_t { text = 1u, value = 2u, localClock = 3u, unknown = 0xffu };
enum class HistoryReducer : std::uint8_t { none, last, mean, minimum, maximum };

struct PackageInfo {
    std::uint32_t totalSize{};
    std::uint32_t payloadCrc32{};
    std::uint16_t sourceCount{};
    std::uint16_t sectionCount{};
    std::uint16_t pageCount{};
    std::uint16_t widgetCount{};
    std::uint32_t historyBytes{};
    std::uint32_t sourceCrc32{};
};

struct RecordView {
    RecordType type{};
    std::uint8_t version{};
    const std::uint8_t* payload{};
    std::uint16_t payloadSize{};
    std::size_t offset{};
    std::size_t nextOffset{};
};

struct SourceView {
    std::uint16_t sectionIndex{noIndex};
    std::uint16_t sourceIndex{noIndex};
    std::uint32_t subscribePeriodMs{};
    std::uint32_t staleAfterMs{};
    std::uint32_t historyIntervalMs{};
    std::uint16_t historyPoints{};
    HistoryReducer historyReducer{HistoryReducer::none};
    bool permanent{};
    std::string_view provider{};
    std::string_view id{};
    std::string_view path{};
    std::string_view unit{};
};

struct SectionView {
    std::uint16_t sectionIndex{noIndex};
    std::string_view id{};
    std::string_view title{};
    std::string_view previousPageLabel{};
    std::string_view nextPageLabel{};
    std::string_view action1Label{};
    std::string_view action2Label{};
    bool showButtonLabels{};
};

struct PageView {
    std::uint16_t sectionIndex{noIndex};
    std::uint16_t pageIndex{noIndex};
    std::uint8_t columns{};
    std::uint8_t rows{};
    std::uint8_t gap{};
    std::string_view id{};
    std::string_view title{};
};

struct WidgetView {
    std::uint16_t sectionIndex{noIndex};
    std::uint16_t pageIndex{noIndex};
    std::uint16_t sourceIndex{noIndex};
    WidgetType type{WidgetType::unknown};
    std::uint8_t column{};
    std::uint8_t row{};
    std::uint8_t columnSpan{1u};
    std::uint8_t rowSpan{1u};
    std::int8_t decimals{};
    std::string_view typeName{};
    std::string_view sourceId{};
    std::string_view label{};
    std::string_view displayUnit{};
    std::string_view text{};
};

class PackageView {
public:
    PackageView() = default;
    PackageView(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    bool valid() const;
    PackageInfo info() const;
    std::size_t size() const { return size_; }
    bool first(RecordView& record) const;
    bool next(RecordView& record) const;
    bool source(const RecordView& record, SourceView& source) const;
    bool section(const RecordView& record, SectionView& section) const;
    bool page(const RecordView& record, PageView& page) const;
    bool widget(const RecordView& record, WidgetView& widget) const;
    bool sourceByIndex(std::uint16_t index, SourceView& source) const;

private:
    bool recordAt(std::size_t offset, RecordView& record) const;
    const std::uint8_t* data_{};
    std::size_t size_{};
};

std::uint32_t crc32(const std::uint8_t* data, std::size_t size);

} // namespace semantic_display
