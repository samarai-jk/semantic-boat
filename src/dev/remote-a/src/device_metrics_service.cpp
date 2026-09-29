#include "device_metrics_service.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>
extern "C" {
#include "main.h"
void* _sbrk(std::ptrdiff_t increment);
extern std::uint8_t _end;
extern std::uint8_t _estack;
extern std::uint8_t __flash_start__;
extern std::uint8_t __flash_end__;
extern std::uint8_t __flash_image_end__;
extern std::uint8_t __ram_start__;
extern std::uint8_t __ram_static_end__;
extern std::uint8_t __ram2_start__;
extern std::uint8_t __ram2_end__;
extern std::uint8_t __ram2_static_end__;
}

namespace remote_a {
namespace {

constexpr std::string_view localProvider{"device"};
constexpr std::uint32_t updateIntervalMs = 1000u;
constexpr std::uint32_t stackPattern = 0xa5a5a5a5u;
constexpr std::uintptr_t stackPaintGuardBytes = 64u;
std::uint32_t* stackPaintBegin{};
std::uint32_t* stackPaintEnd{};

std::uintptr_t address(const std::uint8_t& symbol) {
    return reinterpret_cast<std::uintptr_t>(&symbol);
}

float kibibytes(std::uintptr_t bytes) {
    return static_cast<float>(bytes) / 1024.0f;
}

struct MemorySnapshot {
    std::uintptr_t flashUsed{};
    std::uintptr_t flashTotal{};
    std::uintptr_t ramStatic{};
    std::uintptr_t ramTotal{};
    std::uintptr_t ramHeadroom{};
    std::uintptr_t heapUsed{};
    std::uintptr_t stackPeak{};
    std::uintptr_t ram2Used{};
    std::uintptr_t ram2Total{};
};

MemorySnapshot memorySnapshot() {
    const auto heapBegin = address(_end);
    const auto heapEnd = reinterpret_cast<std::uintptr_t>(_sbrk(0));
    auto scan = reinterpret_cast<std::uint32_t*>(
        heapEnd > reinterpret_cast<std::uintptr_t>(stackPaintBegin)
            ? (heapEnd + 3u) & ~std::uintptr_t{3u}
            : reinterpret_cast<std::uintptr_t>(stackPaintBegin));
    while (scan && scan < stackPaintEnd && *scan == stackPattern) ++scan;
    const auto stackLowWater = reinterpret_cast<std::uintptr_t>(scan);
    const auto stackTop = address(_estack);

    MemorySnapshot result{};
    result.flashUsed = address(__flash_image_end__) - address(__flash_start__);
    result.flashTotal = address(__flash_end__) - address(__flash_start__);
    result.ramStatic = address(__ram_static_end__) - address(__ram_start__);
    result.ramTotal = address(_estack) - address(__ram_start__);
    result.heapUsed = heapEnd > heapBegin ? heapEnd - heapBegin : 0u;
    result.stackPeak = stackTop > stackLowWater ? stackTop - stackLowWater : 0u;
    result.ramHeadroom = stackLowWater > heapEnd ? stackLowWater - heapEnd : 0u;
    result.ram2Used = address(__ram2_static_end__) - address(__ram2_start__);
    result.ram2Total = address(__ram2_end__) - address(__ram2_start__);
    return result;
}

} // namespace

void DeviceMetricsService::beginStackMonitoring() {
    const auto heapEnd = reinterpret_cast<std::uintptr_t>(_sbrk(0));
    if (heapEnd == std::numeric_limits<std::uintptr_t>::max()) return;
    const auto begin = (heapEnd + 3u) & ~std::uintptr_t{3u};
    const auto stackPointer = static_cast<std::uintptr_t>(__get_MSP());
    if (stackPointer <= begin + stackPaintGuardBytes) return;
    const auto end = (stackPointer - stackPaintGuardBytes) & ~std::uintptr_t{3u};
    stackPaintBegin = reinterpret_cast<std::uint32_t*>(begin);
    stackPaintEnd = reinterpret_cast<std::uint32_t*>(end);
    for (auto* word = stackPaintBegin; word < stackPaintEnd; ++word) {
        *word = stackPattern;
    }
}

bool DeviceMetricsService::init() {
    if (!runtime_.millis || !package_.valid()) return false;
    update(runtime_.millis());
    return true;
}

void DeviceMetricsService::setDeveloperMode(bool enabled) {
    if (developerMode_ == enabled) return;
    developerMode_ = enabled;
    // Populate the header before the setup dialog closes and reveals the page.
    // This is a one-off sample; periodic sampling remains in run().
    if (enabled && runtime_.millis) update(runtime_.millis());
}

bool DeviceMetricsService::publishNumber(const semantic_display::SourceView& source, float value,
                                         std::uint32_t now) {
    if (!semantic_display::sourceIsSubscribed(source, display_.activeSection())) return false;
    const auto* current = data_.get(source.sourceIndex);
    const bool changed = !current || current->type != semantic_display::ValueType::number ||
                         current->number != value;
    if (!data_.setNumber(source.sourceIndex, value, now)) return false;
    if (changed) display_.sourceUpdated(source.sourceIndex, false);
    return true;
}

bool DeviceMetricsService::publishText(const semantic_display::SourceView& source,
                                       const char* value, std::uint32_t now,
                                       bool requestRender) {
    if (!semantic_display::sourceIsSubscribed(source, display_.activeSection())) return false;
    const auto* current = data_.get(source.sourceIndex);
    const bool changed = !current || current->type != semantic_display::ValueType::text ||
        std::string_view{current->text, current->textLength} != value;
    if (!data_.setText(source.sourceIndex, value, now)) return false;
    if (changed) display_.sourceUpdated(source.sourceIndex, requestRender);
    return true;
}

void DeviceMetricsService::update(std::uint32_t now) {
    MemorySnapshot memory{};
    bool memoryReady{};
    if (developerMode_) {
        memory = memorySnapshot();
        memoryReady = true;
        const auto headroomTenths = static_cast<unsigned>(
            (memory.ramHeadroom * 10u + 512u) / 1024u);
        const auto stackTenths = static_cast<unsigned>(
            (memory.stackPeak * 10u + 512u) / 1024u);
        std::snprintf(headerStatus_.data(), headerStatus_.size(), "R%u.%u S%u.%u",
                      headroomTenths / 10u, headroomTenths % 10u,
                      stackTenths / 10u, stackTenths % 10u);
    }
    const auto package = package_.info();
    semantic_display::RecordView record{};
    if (!package_.first(record)) return;
    do {
        semantic_display::SourceView source{};
        if (!package_.source(record, source) || source.provider != localProvider) continue;
        if (!semantic_display::sourceIsSubscribed(source, display_.activeSection())) continue;
        const auto path = source.path;
        if (!memoryReady && path.size() >= 7u && path.substr(0u, 7u) == "memory.") {
            memory = memorySnapshot();
            memoryReady = true;
        }
        if (path == "system.uptime") {
            (void)publishNumber(source, static_cast<float>(now) / 1000.0f, now);
        } else if (path == "system.cpu.clock") {
            (void)publishNumber(source,
                static_cast<float>(SystemCoreClock) / 1000000.0f, now);
        } else if (path == "memory.flash.used") {
            (void)publishNumber(source, kibibytes(memory.flashUsed), now);
        } else if (path == "memory.flash.free") {
            (void)publishNumber(source,
                kibibytes(memory.flashTotal - memory.flashUsed), now);
        } else if (path == "memory.flash.utilization") {
            (void)publishNumber(source,
                100.0f * static_cast<float>(memory.flashUsed) /
                    static_cast<float>(memory.flashTotal), now);
        } else if (path == "memory.ram.static") {
            (void)publishNumber(source, kibibytes(memory.ramStatic), now);
        } else if (path == "memory.ram.headroom") {
            (void)publishNumber(source, kibibytes(memory.ramHeadroom), now);
        } else if (path == "memory.heap.used") {
            (void)publishNumber(source, kibibytes(memory.heapUsed), now);
        } else if (path == "memory.stack.peak") {
            (void)publishNumber(source, kibibytes(memory.stackPeak), now);
        } else if (path == "memory.ram2.used") {
            (void)publishNumber(source, kibibytes(memory.ram2Used), now);
        } else if (path == "memory.ram2.free") {
            (void)publishNumber(source,
                kibibytes(memory.ram2Total - memory.ram2Used), now);
        } else if (path == "display.config.bytes") {
            (void)publishNumber(source, kibibytes(package.totalSize), now);
        } else if (path == "display.history.bytes") {
            (void)publishNumber(source, kibibytes(history_.usedBytes()), now);
        } else if (path == "display.sources") {
            (void)publishNumber(source, package.sourceCount, now);
        } else if (path == "display.sections") {
            (void)publishNumber(source, package.sectionCount, now);
        } else if (path == "display.pages") {
            (void)publishNumber(source, package.pageCount, now);
        } else if (path == "display.widgets") {
            (void)publishNumber(source, package.widgetCount, now);
        } else if (path == "display.partial.refreshes") {
            (void)publishNumber(source, display_.partialRefreshCount(), now);
        } else if (path == "communication.uart.dropped") {
            (void)publishNumber(source, transport_.droppedBytes(), now);
        } else if (path == "system.events.dropped") {
            (void)publishNumber(source, events_.droppedEvents(), now);
        } else if (path == "communication.link.status") {
            (void)publishText(source, link_.connected() ? "ONLINE" : "WAITING", now, true);
        }
    } while (package_.next(record));
}

void DeviceMetricsService::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - updateAt_) < 0) return;
    updateAt_ = now + updateIntervalMs;
    update(now);
}

} // namespace remote_a
