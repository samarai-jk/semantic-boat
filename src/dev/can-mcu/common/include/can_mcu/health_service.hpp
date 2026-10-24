#pragma once

#include <array>
#include <cstdint>
#include "can_mcu/mcu_monitor.hpp"
#include "slstm32/console.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"
#include "slstm32/storage/byte_storage.hpp"
#include "slstm32/storage/redundant_blob_store.hpp"

namespace can_mcu {

// Long-term health record for unattended runs, kept in EEPROM so that it
// survives power loss and does not depend on a PC capturing the log:
// restarts per cause (including fatal errors reported through the crash
// record and hangs caught by the watchdog), supply drops seen by the power
// monitor, and the previous run's duration and lowest supply voltage (saved
// every minute). At startup the previous run and the totals are logged;
// supply drops are logged as they happen. Console: "health [clear]".
class HealthService final : public slstm32::Service {
public:
    HealthService(slstm32::Runtime runtime, slstm32::Logger log, slstm32::storage::ByteStorage& storage,
                  std::uint32_t offset, can_mcu::McuMonitor& mcu)
        : runtime_(runtime), log_(log), store_(storage, offset, slstm32::storage::RedundantBlobStore::requiredBytes),
          mcu_(mcu) {}

    bool init() override;
    void run() override;
    bool registerCommands(slstm32::CommandConsole& console);

    // True once anything noteworthy happened in this run or ended the
    // previous one: a fatal error or watchdog reset, a supply drop below the
    // power monitor threshold, or a supply reading below 3.1 V.
    bool issueSeen() const;

private:
    enum Counter : std::size_t { powerOn, pin, software, watchdog, fatal, other, counters };
    struct Record {
        std::array<std::uint16_t, counters> resets{};
        std::uint32_t powerDropsTotal{};
        std::uint32_t lastRunSeconds{};
        std::uint16_t lastRunLowestMv{};
        std::uint16_t lastRunPowerDrops{};
    };

    static void healthCommand(void* context, std::size_t argc, const char* const* argv);
    bool load();
    bool save();
    void saveRun(std::uint32_t now);
    void logTotals() const;

    slstm32::Runtime runtime_;
    slstm32::Logger log_;
    slstm32::storage::RedundantBlobStore store_;
    can_mcu::McuMonitor& mcu_;
    Record record_{};
    std::uint32_t reportedDrops_{};
    std::uint32_t dropsAtStart_{};
    std::uint32_t nextSaveAt_{};
    bool storageOk_{};
    bool previousRunFailed_{};
};

} // namespace can_mcu
