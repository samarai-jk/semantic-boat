#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "display_link_service.hpp"
#include "semantic_display/display.hpp"
#include "semantic_display/history.hpp"
#include "semantic_display/package.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"
#include "slstm32/storage/byte_storage.hpp"
#include "slstm32/storage/redundant_blob_store.hpp"

namespace remote_a {

// Graph history from the gateway's database (semantic-link PROTOCOL.md,
// "History"), and the graphs' time frames:
// - 30 s after the server is first seen, each history source is loaded once,
//   one request at a time, so the graphs are complete without waiting;
//   again 5 s after waking from sleep (the clock stops while sleeping);
// - a new time frame loads the sources of its graph at once and is stored:
//   the last 8 graphs keep their frame across restarts (least recently used
//   replaced), restored at start-up.
class HistoryService final : public slstm32::Service {
public:
    HistoryService(slstm32::Runtime runtime, DisplayLinkService& link, semantic_display::DisplayService& display,
                   semantic_display::HistoryStore& history, const semantic_display::PackageView& package,
                   slstm32::storage::ByteStorage& storage, std::uint32_t storageOffset)
        : runtime_(runtime), link_(link), display_(display), history_(history), package_(package),
          frames_(storage, storageOffset, slstm32::storage::RedundantBlobStore::requiredBytes) {}

    bool init() override;
    void run() override;
    semantic_display::HistoryHandler handler() { return {this, &timeFrameChangedThunk}; }

private:
    static constexpr std::size_t frameSlots = 8u;
    static constexpr std::size_t queueCapacity = 16u;
    struct Slot {
        std::uint32_t key{}; // 24 bits of the page's CRC; 0: free
        std::uint8_t frame{};
    };

    static void timeFrameChangedThunk(void* context, std::uint16_t pageIndex, std::uint8_t frame);
    static void dataThunk(void* context, std::uint16_t requestId, std::uint16_t sourceIndex, std::uint32_t intervalMs,
                          std::uint16_t total, std::uint16_t first, const float* values, std::size_t count);
    std::uint32_t pageKey(std::uint16_t pageIndex) const;
    void loadFrames();
    void saveFrame(std::uint32_t key, std::uint8_t frame);
    void enqueue(std::uint16_t sourceIndex, bool urgent);
    void enqueuePlot(std::uint16_t pageIndex, bool urgent);
    void sendNext(std::uint32_t now);

    slstm32::Runtime runtime_;
    DisplayLinkService& link_;
    semantic_display::DisplayService& display_;
    semantic_display::HistoryStore& history_;
    const semantic_display::PackageView& package_;
    slstm32::storage::RedundantBlobStore frames_;
    std::array<Slot, frameSlots> slots_{};
    std::array<std::uint16_t, queueCapacity> queue_{};
    std::size_t queued_{};
    std::uint32_t connectedAt_{};
    std::uint32_t requestedAt_{};
    std::uint16_t requestId_{};
    std::uint16_t inFlight_{semantic_display::noIndex};
    bool wasConnected_{};
    bool wasSleeping_{};
    bool initialLoadDone_{};
};

} // namespace remote_a
