#include "health_service.hpp"
#include <cstring>
#include "can_mcu/board.hpp"
#include "can_mcu/supervision.hpp"

namespace lab_test_01 {
namespace {

using slstm32::LogLevel;

constexpr std::uint8_t recordVersion = 1u;
constexpr std::size_t recordBytes = 1u + 2u * 6u + 4u + 4u + 2u + 2u;
constexpr std::uint32_t savePeriodMs = 60000u;
constexpr std::uint16_t lowSupplyMv = 3100u;

void put16(std::uint8_t*& out, std::uint16_t value) {
    *out++ = static_cast<std::uint8_t>(value);
    *out++ = static_cast<std::uint8_t>(value >> 8u);
}

void put32(std::uint8_t*& out, std::uint32_t value) {
    put16(out, static_cast<std::uint16_t>(value));
    put16(out, static_cast<std::uint16_t>(value >> 16u));
}

std::uint16_t get16(const std::uint8_t*& in) {
    const auto value = static_cast<std::uint16_t>(in[0] | in[1] << 8u);
    in += 2;
    return value;
}

std::uint32_t get32(const std::uint8_t*& in) {
    const std::uint32_t low = get16(in);
    return low | static_cast<std::uint32_t>(get16(in)) << 16u;
}

const char* crashName(can_mcu::CrashRecord::Kind kind) {
    switch (kind) {
    case can_mcu::CrashRecord::Kind::hardFault: return "HardFault";
    case can_mcu::CrashRecord::Kind::errorHandler: return "Error_Handler";
    default: return "?";
    }
}

} // namespace

bool HealthService::load() {
    std::uint8_t data[slstm32::storage::RedundantBlobStore::maxPayloadBytes]{};
    std::size_t size = 0u;
    if (!store_.load(data, sizeof data, size) || size < recordBytes || data[0] != recordVersion) return false;
    const std::uint8_t* in = data + 1;
    for (auto& count : record_.resets) count = get16(in);
    record_.powerDropsTotal = get32(in);
    record_.lastRunSeconds = get32(in);
    record_.lastRunLowestMv = get16(in);
    record_.lastRunPowerDrops = get16(in);
    return true;
}

bool HealthService::save() {
    std::uint8_t data[recordBytes]{recordVersion};
    std::uint8_t* out = data + 1;
    for (const auto count : record_.resets) put16(out, count);
    put32(out, record_.powerDropsTotal);
    put32(out, record_.lastRunSeconds);
    put16(out, record_.lastRunLowestMv);
    put16(out, record_.lastRunPowerDrops);
    return store_.commit(data, sizeof data);
}

bool HealthService::init() {
    const bool loaded = load();
    if (loaded && record_.lastRunSeconds != 0u) {
        log_.writef(LogLevel::info, "health: previous run alive >= %luh%02lum%02lus, lowest VDDA %u mV, %u power drop(s)",
                    static_cast<unsigned long>(record_.lastRunSeconds / 3600u),
                    static_cast<unsigned long>(record_.lastRunSeconds / 60u % 60u),
                    static_cast<unsigned long>(record_.lastRunSeconds % 60u),
                    static_cast<unsigned>(record_.lastRunLowestMv), static_cast<unsigned>(record_.lastRunPowerDrops));
    } else if (!loaded) {
        log_.write(LogLevel::info, "health: no record yet, starting one");
    }

    const auto crash = can_mcu::takeCrashRecord();
    const auto cause = can_mcu::resetCauseCode();
    if (crash.kind != can_mcu::CrashRecord::Kind::none) {
        ++record_.resets[fatal];
        previousRunFailed_ = true;
        log_.writef(LogLevel::error, "health: previous run ended by %s after %lu s (CFSR %08lX HFSR %08lX BFAR %08lX MMFAR %08lX)",
                    crashName(crash.kind), static_cast<unsigned long>(crash.uptimeMs / 1000u),
                    static_cast<unsigned long>(crash.cfsr), static_cast<unsigned long>(crash.hfsr),
                    static_cast<unsigned long>(crash.bfar), static_cast<unsigned long>(crash.mmfar));
    } else {
        switch (cause) {
        case can_mcu::ResetCause::powerOn: ++record_.resets[powerOn]; break;
        case can_mcu::ResetCause::pin: ++record_.resets[pin]; break;
        case can_mcu::ResetCause::software: ++record_.resets[software]; break;
        case can_mcu::ResetCause::independentWatchdog:
        case can_mcu::ResetCause::windowWatchdog:
            ++record_.resets[watchdog];
            previousRunFailed_ = true;
            log_.write(LogLevel::error, "health: previous run hung, restarted by the watchdog");
            break;
        default: ++record_.resets[other]; break;
        }
    }
    logTotals();

    dropsAtStart_ = can_mcu::powerDrops();
    reportedDrops_ = dropsAtStart_;
    record_.lastRunSeconds = 0u;
    record_.lastRunLowestMv = 0xFFFFu;
    record_.lastRunPowerDrops = 0u;
    storageOk_ = save();
    if (!storageOk_) log_.write(LogLevel::error, "health: record could not be saved");
    nextSaveAt_ = runtime_.millis() + savePeriodMs;
    return true;
}

void HealthService::run() {
    const auto drops = can_mcu::powerDrops();
    if (drops != reportedDrops_) {
        log_.writef(LogLevel::error, "power: VDD fell below 2.9 V (%lu new, %lu this run), VDDA now %u mV",
                    static_cast<unsigned long>(drops - reportedDrops_),
                    static_cast<unsigned long>(drops - dropsAtStart_), static_cast<unsigned>(mcu_.supplyMv()));
        reportedDrops_ = drops;
    }
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextSaveAt_) >= 0) {
        nextSaveAt_ += savePeriodMs;
        saveRun(now);
    }
}

void HealthService::saveRun(std::uint32_t now) {
    const auto dropsThisRun = can_mcu::powerDrops() - dropsAtStart_;
    record_.powerDropsTotal += dropsThisRun - record_.lastRunPowerDrops;
    record_.lastRunPowerDrops = static_cast<std::uint16_t>(dropsThisRun > 0xFFFFu ? 0xFFFFu : dropsThisRun);
    record_.lastRunSeconds = now / 1000u;
    record_.lastRunLowestMv = mcu_.lowestMv();
    storageOk_ = save();
    if (!storageOk_) log_.write(LogLevel::error, "health: record could not be saved");
}

bool HealthService::issueSeen() const {
    return previousRunFailed_ || can_mcu::powerDrops() != dropsAtStart_ || mcu_.lowestMv() < lowSupplyMv;
}

void HealthService::logTotals() const {
    log_.writef(LogLevel::info, "health: resets power-on %u, pin %u, software %u, watchdog %u, fatal %u, other %u; power drops %lu",
                static_cast<unsigned>(record_.resets[powerOn]), static_cast<unsigned>(record_.resets[pin]),
                static_cast<unsigned>(record_.resets[software]), static_cast<unsigned>(record_.resets[watchdog]),
                static_cast<unsigned>(record_.resets[fatal]), static_cast<unsigned>(record_.resets[other]),
                static_cast<unsigned long>(record_.powerDropsTotal));
}

bool HealthService::registerCommands(slstm32::CommandConsole& console) {
    return console.add({"health", "health [clear]  restart and supply statistics", &healthCommand, this});
}

void HealthService::healthCommand(void* context, std::size_t argc, const char* const* argv) {
    auto& self = *static_cast<HealthService*>(context);
    if (argc >= 2u && std::strcmp(argv[1], "clear") == 0) {
        self.record_ = {};
        self.dropsAtStart_ = can_mcu::powerDrops();
        self.reportedDrops_ = self.dropsAtStart_;
        self.previousRunFailed_ = false;
        self.saveRun(self.runtime_.millis());
        self.log_.write(LogLevel::info, "health: statistics cleared");
    }
    const auto now = self.runtime_.millis();
    self.log_.writef(LogLevel::info, "health: this run up %lus, lowest VDDA %u mV, %lu power drop(s), %s",
                     static_cast<unsigned long>(now / 1000u), static_cast<unsigned>(self.mcu_.lowestMv()),
                     static_cast<unsigned long>(can_mcu::powerDrops() - self.dropsAtStart_),
                     self.issueSeen() ? "ISSUE SEEN" : "no issues");
    self.logTotals();
}

} // namespace lab_test_01
