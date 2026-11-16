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
// A history plot shows its own value plus up to this many more.
inline constexpr std::size_t maxHistoryPlotExtraSeries = 2u;
inline constexpr std::uint8_t defaultValueMaxDigits = 6u;
inline constexpr std::uint8_t maximumValueMaxDigits = 32u;

enum class RecordType : std::uint8_t { source = 1u, section = 2u, page = 3u, widget = 4u, historyPlot = 5u };
enum class WidgetType : std::uint8_t { text = 1u, value = 2u, localClock = 3u, gauge = 4u, unknown = 0xffu };
// A gauge shows up to this many further values: numbers inside the dial,
// chips lit while their value is above a threshold, values beside the dial.
inline constexpr std::size_t maxGaugeReferences = 10u;
inline constexpr std::size_t gaugeFixedBytes = 29u;     // seven floats and the reference count
inline constexpr std::size_t gaugeReferenceBytes = 11u; // per reference, before its strings
enum class PositionFormat : std::uint8_t { ddm, dd, dms };
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
    std::uint32_t outdatedAfterMs{};
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
    std::uint8_t maxDigits{defaultValueMaxDigits};
    PositionFormat positionFormat{PositionFormat::ddm};
    std::string_view typeName{};
    std::string_view sourceId{};
    std::string_view label{};
    std::string_view displayUnit{};
    // value: state names separated by '|' ("OFF|ON"), chosen by the rounded
    // value; text: the text.
    std::string_view text{};
    // gauge: the dial's settings and further values (PackageView::gauge).
    const std::uint8_t* extra{};
    std::uint16_t extraSize{};
};

enum class GaugeReferenceKind : std::uint8_t { value = 0u, chip = 1u, side = 2u };

// A further value of a gauge: a number inside the dial ("607.1 H"), a chip
// lit while the value is above its threshold ("IGN"), or a value in the
// column beside the dial (text is its label).
struct GaugeReference {
    GaugeReferenceKind kind{GaugeReferenceKind::value};
    std::uint8_t decimals{};
    float threshold{0.5f};
    std::uint16_t sourceIndex{noIndex};
    std::string_view sourceId{};
    std::string_view text{};
    std::string_view displayUnit{};
};

// A round dial over 270 degrees: range, redline (NaN: none), tick spacing,
// labels every labelStep divided by labelDivisor (3000 rpm as "30").
struct GaugeView {
    float minimum{}, maximum{}, redline{}, major{}, minor{}, labelStep{}, labelDivisor{1.0f};
    std::uint8_t referenceCount{};
    GaugeReference references[maxGaugeReferences]{};
};

// One value of a graph. circular: a direction that wraps (the line breaks
// instead of crossing the graph).
struct HistoryPlotSeries {
    std::uint16_t sourceIndex{noIndex};
    std::uint8_t decimals{1u};
    bool fixedRange{};
    bool circular{};
    float minimum{};
    float maximum{};
    std::string_view sourceId{};
    std::string_view title{};
    std::string_view displayUnit{};
};

struct HistoryPlotView {
    std::uint16_t sectionIndex{noIndex};
    std::uint16_t pageIndex{noIndex};
    std::uint16_t sourceIndex{noIndex};
    std::uint8_t decimals{1u};
    bool fixedRange{};
    bool circular{};
    float minimum{};
    float maximum{};
    std::string_view sourceId{};
    std::string_view title{};
    std::string_view displayUnit{};
    // Further values drawn dotted, then dashed, each on its own scale.
    std::uint8_t extraCount{};
    HistoryPlotSeries extra[maxHistoryPlotExtraSeries]{};
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
    bool gauge(const WidgetView& widget, GaugeView& gauge) const;
    bool historyPlot(const RecordView& record, HistoryPlotView& plot) const;
    bool historyPlotByPage(std::uint16_t index, HistoryPlotView& plot) const;
    bool sourceByIndex(std::uint16_t index, SourceView& source) const;

private:
    bool headerValid() const;
    bool recordAt(std::size_t offset, RecordView& record) const;
    const std::uint8_t* data_{};
    std::size_t size_{};
};

std::uint32_t crc32(const std::uint8_t* data, std::size_t size);

} // namespace semantic_display
