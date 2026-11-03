#pragma once

#include <cstddef>
#include <cstdint>
#include "semantic_display/package.hpp"

namespace semantic_display {

class HistoryStore {
public:
    HistoryStore(std::uint8_t* memory, std::size_t capacity) : memory_(memory), capacity_(capacity) {}

    bool configure(const PackageView& package, std::uint32_t now);
    bool ingest(std::uint16_t sourceIndex, float value, std::uint32_t now);
    std::size_t usedBytes() const { return usedBytes_; }
    std::size_t seriesCount() const { return seriesCount_; }
    std::size_t sampleCount(std::uint16_t sourceIndex) const;
    std::uint32_t revision() const { return revision_; }
    bool sample(std::uint16_t sourceIndex, std::size_t oldestIndex, float& value) const;
    // Retained points and the current sample interval of a source (0: none).
    std::size_t points(std::uint16_t sourceIndex) const;
    std::uint32_t intervalMs(std::uint16_t sourceIndex) const;
    HistoryReducer reducer(std::uint16_t sourceIndex) const;
    // Another time frame: a new interval for the same points; the ring starts empty.
    bool setInterval(std::uint16_t sourceIndex, std::uint32_t intervalMs, std::uint32_t now);
    // Points loaded from a database, oldest first (NaN: no data), ending now.
    // The first chunk (first == 0) replaces the ring; an interval other than
    // the current one is refused (the time frame changed meanwhile).
    bool fill(std::uint16_t sourceIndex, std::uint32_t intervalMs, std::uint16_t total, std::uint16_t first,
              const float* values, std::size_t count);

private:
    std::uint8_t* find(std::uint16_t sourceIndex) const;
    std::uint8_t* memory_{};
    std::size_t capacity_{};
    std::size_t usedBytes_{};
    std::size_t seriesCount_{};
    std::uint32_t revision_{};
};

} // namespace semantic_display
