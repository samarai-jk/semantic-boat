#include "semantic_display/history.hpp"
#include <cstring>

namespace semantic_display {
namespace {

constexpr std::size_t headerSize = historySeriesOverheadBytes;
constexpr std::size_t sourceOffset = 0u;
constexpr std::size_t pointsOffset = 2u;
constexpr std::size_t writeOffset = 4u;
constexpr std::size_t countOffset = 6u;
constexpr std::size_t intervalOffset = 8u;
constexpr std::size_t nextOffset = 12u;
constexpr std::size_t reducerOffset = 16u;
constexpr std::size_t activeOffset = 17u;
constexpr std::size_t accumulatorOffset = 20u;
constexpr std::size_t auxiliaryOffset = 24u;
constexpr std::size_t accumulatorCountOffset = 28u;

std::uint16_t read16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8u);
}
std::uint32_t read32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8u) |
           (static_cast<std::uint32_t>(data[2]) << 16u) | (static_cast<std::uint32_t>(data[3]) << 24u);
}
void write16(std::uint8_t* data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value); data[1] = static_cast<std::uint8_t>(value >> 8u);
}
void write32(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value); data[1] = static_cast<std::uint8_t>(value >> 8u);
    data[2] = static_cast<std::uint8_t>(value >> 16u); data[3] = static_cast<std::uint8_t>(value >> 24u);
}
float readFloat(const std::uint8_t* data) { float result{}; std::memcpy(&result, data, sizeof result); return result; }
void writeFloat(std::uint8_t* data, float value) { std::memcpy(data, &value, sizeof value); }
std::size_t recordSize(const std::uint8_t* data) { return headerSize + read16(data + pointsOffset) * sizeof(float); }

} // namespace

bool HistoryStore::configure(const PackageView& package, std::uint32_t now) {
    usedBytes_ = 0u;
    seriesCount_ = 0u;
    if (!memory_ || !package.valid() || package.info().historyBytes > capacity_) return false;
    std::memset(memory_, 0, capacity_);
    RecordView record{};
    if (!package.first(record)) return package.info().historyBytes == 0u;
    do {
        SourceView source{};
        if (!package.source(record, source) || source.historyPoints == 0u) continue;
        const auto bytes = headerSize + static_cast<std::size_t>(source.historyPoints) * sizeof(float);
        if (usedBytes_ + bytes > capacity_) return false;
        auto* series = memory_ + usedBytes_;
        write16(series + sourceOffset, source.sourceIndex);
        write16(series + pointsOffset, source.historyPoints);
        write32(series + intervalOffset, source.historyIntervalMs);
        write32(series + nextOffset, now + source.historyIntervalMs);
        series[reducerOffset] = static_cast<std::uint8_t>(source.historyReducer);
        usedBytes_ += bytes;
        ++seriesCount_;
    } while (package.next(record));
    return true;
}

std::uint8_t* HistoryStore::find(std::uint16_t sourceIndex) const {
    for (std::size_t offset = 0u; offset < usedBytes_;) {
        auto* series = memory_ + offset;
        if (read16(series + sourceOffset) == sourceIndex) return series;
        offset += recordSize(series);
    }
    return nullptr;
}

bool HistoryStore::ingest(std::uint16_t sourceIndex, float value, std::uint32_t now) {
    auto* series = find(sourceIndex);
    if (!series) return false;
    const auto reducer = static_cast<HistoryReducer>(series[reducerOffset]);
    auto accumulator = readFloat(series + accumulatorOffset);
    auto auxiliary = readFloat(series + auxiliaryOffset);
    auto accumulatorCount = read32(series + accumulatorCountOffset);
    if (series[activeOffset] == 0u) {
        accumulator = value;
        auxiliary = value;
        accumulatorCount = 1u;
        series[activeOffset] = 1u;
    } else {
        if (reducer == HistoryReducer::mean) accumulator += value;
        else if (reducer == HistoryReducer::minimum && value < accumulator) accumulator = value;
        else if (reducer == HistoryReducer::maximum && value > accumulator) accumulator = value;
        else if (reducer == HistoryReducer::last) accumulator = value;
        auxiliary = value;
        ++accumulatorCount;
    }
    writeFloat(series + accumulatorOffset, accumulator);
    writeFloat(series + auxiliaryOffset, auxiliary);
    write32(series + accumulatorCountOffset, accumulatorCount);

    const auto deadline = read32(series + nextOffset);
    if (static_cast<std::int32_t>(now - deadline) < 0) return true;
    auto sampleValue = accumulator;
    if (reducer == HistoryReducer::mean && accumulatorCount != 0u) {
        sampleValue = accumulator / static_cast<float>(accumulatorCount);
    }
    const auto points = read16(series + pointsOffset);
    auto writeIndex = read16(series + writeOffset);
    writeFloat(series + headerSize + writeIndex * sizeof(float), sampleValue);
    writeIndex = static_cast<std::uint16_t>((writeIndex + 1u) % points);
    write16(series + writeOffset, writeIndex);
    auto count = read16(series + countOffset);
    if (count < points) write16(series + countOffset, static_cast<std::uint16_t>(count + 1u));
    const auto interval = read32(series + intervalOffset);
    auto next = deadline;
    do { next += interval; } while (static_cast<std::int32_t>(now - next) >= 0);
    write32(series + nextOffset, next);
    series[activeOffset] = 0u;
    write32(series + accumulatorCountOffset, 0u);
    return true;
}

std::size_t HistoryStore::sampleCount(std::uint16_t sourceIndex) const {
    const auto* series = find(sourceIndex);
    return series ? read16(series + countOffset) : 0u;
}

bool HistoryStore::sample(std::uint16_t sourceIndex, std::size_t oldestIndex, float& value) const {
    const auto* series = find(sourceIndex);
    if (!series) return false;
    const auto count = read16(series + countOffset);
    const auto points = read16(series + pointsOffset);
    if (oldestIndex >= count) return false;
    const auto writeIndex = read16(series + writeOffset);
    const auto first = count == points ? writeIndex : 0u;
    const auto physical = (first + oldestIndex) % points;
    value = readFloat(series + headerSize + physical * sizeof(float));
    return true;
}

} // namespace semantic_display
