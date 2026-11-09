#include "history_service.hpp"
#include <cstring>

namespace remote_a {
namespace {

namespace display = semantic_display;

constexpr std::uint32_t initialLoadDelayMs = 30000u; // after the server is first seen
constexpr std::uint32_t requestTimeoutMs = 10000u;
// The clock stops while the display sleeps (Stop 2), so the histories miss
// that time: after waking they are loaded again from the server.
constexpr std::uint32_t wakeLoadDelayMs = 5000u;
// Version 2: the list of time frames got 3 days after 24 hours (index 7);
// version 1 indices from 7 on move up by one.
constexpr std::uint8_t framesVersion = 2u;
constexpr std::uint8_t insertedFrame = 7u;

} // namespace

bool HistoryService::init() {
    if (!runtime_.millis) return false;
    link_.setHistoryReceiver({this, &dataThunk});
    loadFrames();
    // Restore the stored time frames of this configuration's graphs.
    for (std::uint16_t page = 0u; page < package_.info().pageCount; ++page) {
        display::HistoryPlotView plot{};
        if (!package_.historyPlotByPage(page, plot)) continue;
        const auto key = pageKey(page);
        for (const auto& slot : slots_) {
            if (slot.key == key && slot.key != 0u) (void)display_.applyTimeFrame(page, slot.frame);
        }
    }
    return true;
}

// 24 bits of the CRC of "section id/page id": stable across reinstalls of
// the same configuration, different for other graphs (never 0).
std::uint32_t HistoryService::pageKey(std::uint16_t pageIndex) const {
    display::RecordView record{};
    display::PageView page{};
    bool found{};
    if (package_.first(record)) do {
        if (package_.page(record, page) && page.pageIndex == pageIndex) { found = true; break; }
    } while (package_.next(record));
    if (!found) return 0u;
    display::SectionView section{};
    bool sectionFound{};
    if (package_.first(record)) do {
        if (package_.section(record, section) && section.sectionIndex == page.sectionIndex) { sectionFound = true; break; }
    } while (package_.next(record));
    std::uint8_t text[96]{};
    std::size_t length{};
    const auto append = [&](std::string_view part) {
        for (const auto character : part) if (length < sizeof text) text[length++] = static_cast<std::uint8_t>(character);
    };
    if (sectionFound) append(section.id);
    append("/");
    append(page.id);
    const auto key = display::crc32(text, length) & 0x00FFFFFFu;
    return key == 0u ? 1u : key;
}

void HistoryService::loadFrames() {
    std::uint8_t payload[slstm32::storage::RedundantBlobStore::maxPayloadBytes]{};
    std::size_t size{};
    if (!frames_.load(payload, sizeof payload, size) || size < 2u) return;
    if (payload[0] != framesVersion && payload[0] != 1u) return;
    const bool older = payload[0] == 1u;
    const auto count = payload[1] < frameSlots ? payload[1] : frameSlots;
    if (size < 2u + 4u * count) return;
    for (std::size_t index = 0u; index < count; ++index) {
        const auto* entry = payload + 2u + 4u * index;
        slots_[index].key = entry[0] | entry[1] << 8u | static_cast<std::uint32_t>(entry[2]) << 16u;
        const auto frame = static_cast<std::uint8_t>(older && entry[3] >= insertedFrame ? entry[3] + 1u : entry[3]);
        slots_[index].frame = frame < display::timeFrameCount ? frame : 0u;
    }
}

// The most recent first; the least recently used falls out.
void HistoryService::saveFrame(std::uint32_t key, std::uint8_t frame) {
    std::size_t position = frameSlots - 1u;
    for (std::size_t index = 0u; index < frameSlots; ++index) {
        if (slots_[index].key == key) { position = index; break; }
    }
    for (auto index = position; index > 0u; --index) slots_[index] = slots_[index - 1u];
    slots_[0] = {key, frame};
    std::uint8_t payload[2u + 4u * frameSlots]{framesVersion, static_cast<std::uint8_t>(frameSlots)};
    for (std::size_t index = 0u; index < frameSlots; ++index) {
        auto* entry = payload + 2u + 4u * index;
        entry[0] = static_cast<std::uint8_t>(slots_[index].key);
        entry[1] = static_cast<std::uint8_t>(slots_[index].key >> 8u);
        entry[2] = static_cast<std::uint8_t>(slots_[index].key >> 16u);
        entry[3] = slots_[index].frame;
    }
    (void)frames_.commit(payload, sizeof payload);
}

void HistoryService::enqueue(std::uint16_t sourceIndex, bool urgent) {
    for (std::size_t index = 0u; index < queued_; ++index) {
        if (queue_[index] != sourceIndex) continue;
        if (!urgent) return;
        for (auto move = index; move + 1u < queued_; ++move) queue_[move] = queue_[move + 1u];
        --queued_;
        break;
    }
    if (queued_ == queue_.size()) return;
    if (urgent) {
        for (auto move = queued_; move > 0u; --move) queue_[move] = queue_[move - 1u];
        queue_[0] = sourceIndex;
    } else {
        queue_[queued_] = sourceIndex;
    }
    ++queued_;
}

void HistoryService::enqueuePlot(std::uint16_t pageIndex, bool urgent) {
    display::HistoryPlotView plot{};
    if (!package_.historyPlotByPage(pageIndex, plot)) return;
    for (std::size_t index = plot.extraCount; index > 0u; --index) enqueue(plot.extra[index - 1u].sourceIndex, urgent);
    enqueue(plot.sourceIndex, urgent);
}

void HistoryService::timeFrameChangedThunk(void* context, std::uint16_t pageIndex, std::uint8_t frame) {
    auto& self = *static_cast<HistoryService*>(context);
    self.saveFrame(self.pageKey(pageIndex), frame);
    self.enqueuePlot(pageIndex, true);
    // The answer to an older request has the old interval and is refused.
    self.inFlight_ = display::noIndex;
}

void HistoryService::dataThunk(void* context, std::uint16_t requestId, std::uint16_t sourceIndex,
                               std::uint32_t intervalMs, std::uint16_t total, std::uint16_t first,
                               const float* values, std::size_t count) {
    auto& self = *static_cast<HistoryService*>(context);
    if (self.history_.fill(sourceIndex, intervalMs, total, first, values, count)) {
        self.display_.historyChanged(sourceIndex);
    }
    if (requestId == self.requestId_ && first + count >= total) self.inFlight_ = display::noIndex;
}

void HistoryService::sendNext(std::uint32_t now) {
    while (queued_ != 0u) {
        const auto sourceIndex = queue_[0];
        for (std::size_t move = 0u; move + 1u < queued_; ++move) queue_[move] = queue_[move + 1u];
        --queued_;
        const auto points = history_.points(sourceIndex);
        if (points == 0u) continue;
        ++requestId_;
        if (!link_.requestHistory(requestId_, sourceIndex, history_.intervalMs(sourceIndex),
                                  static_cast<std::uint16_t>(points),
                                  static_cast<std::uint8_t>(history_.reducer(sourceIndex)))) {
            enqueue(sourceIndex, true); // the link's queue is full: next time
            return;
        }
        inFlight_ = sourceIndex;
        requestedAt_ = now;
        return;
    }
}

void HistoryService::run() {
    const auto now = runtime_.millis();
    const bool sleeping = display_.sleeping();
    if (wasSleeping_ && !sleeping) {
        initialLoadDone_ = false;
        connectedAt_ = now - (initialLoadDelayMs - wakeLoadDelayMs);
    }
    wasSleeping_ = sleeping;
    const bool connected = link_.connected();
    if (connected && !wasConnected_) connectedAt_ = now;
    wasConnected_ = connected;
    if (!connected) return;
    if (!initialLoadDone_ && static_cast<std::uint32_t>(now - connectedAt_) >= initialLoadDelayMs) {
        initialLoadDone_ = true;
        display::RecordView record{};
        if (package_.first(record)) do {
            display::SourceView source{};
            if (package_.source(record, source) && source.historyPoints != 0u) enqueue(source.sourceIndex, false);
        } while (package_.next(record));
    }
    if (inFlight_ != display::noIndex) {
        if (static_cast<std::uint32_t>(now - requestedAt_) < requestTimeoutMs) return;
        inFlight_ = display::noIndex;
    }
    sendNext(now);
}

} // namespace remote_a
