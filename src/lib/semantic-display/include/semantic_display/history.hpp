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
    bool sample(std::uint16_t sourceIndex, std::size_t oldestIndex, float& value) const;

private:
    std::uint8_t* find(std::uint16_t sourceIndex) const;
    std::uint8_t* memory_{};
    std::size_t capacity_{};
    std::size_t usedBytes_{};
    std::size_t seriesCount_{};
};

} // namespace semantic_display
