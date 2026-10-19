#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/event_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"
#include "slstm32/service.hpp"
#include "slstm32/storage/keyed_record_store.hpp"
#include "slstm32/storage/redundant_blob_store.hpp"
#include "slstm32/sensors/window.hpp"

namespace slstm32::sensors {

// Event payload published on every alarm transition (8 bytes).
struct AlarmEvent {
    std::uint8_t channel{};   // index in the registry
    std::uint8_t rule{};      // alarm slot; Measurement::faultSlot for the fault alarm
    Severity severity{};
    AlarmTransition transition{};
    std::int32_t value{};     // channel value at the transition
};

// Summary for annunciators (sound, LEDs): the most severe active alarm and
// whether any active alarm still waits for acknowledgement.
struct AlarmSummary {
    bool active{};
    bool unacknowledged{};
    Severity highest{Severity::warning};            // of all active alarms
    Severity highestUnacknowledged{Severity::warning};
};

// All measurement channels of a device. On init() it checks the
// configuration (unique names, unique instance per quantity), applies the
// settings persisted for each channel's sensor identity, and gives each
// channel its alarm rules, fault alarm, and output: the stored ones, or the
// defaults of its MeasurementConfig, with windows from the device's
// WindowPool. Channels whose identity becomes known or changes later
// (sensors discovered on a bus, a sensor swapped) get their sensor settings
// then. run() ticks every channel for staleness, windows, and alarm delays.
// Alarm transitions are logged and, if an event bus is given, published.
//
// Two kinds of settings, stored apart:
// - calibration belongs to the physical sensor: stored per sensor identity
//   in a KeyedRecordStore, so adding, removing, or reordering sensors never
//   affects the settings of the others;
// - alarm rules, fault alarm, and output belong to the channel (what it is
//   used for): stored per channel in its own area (processingBytes()), five
//   records of 128 bytes per channel, each tagged with the channel's name.
class MeasurementRegistry final : public Service {
public:
    // Records per channel in the processing area: output and fault alarm, rules 0..3.
    static constexpr std::size_t processingRecords = 1u + Measurement::maxAlarms;
    static constexpr std::size_t processingBytes(std::size_t channels) {
        return channels * processingRecords * storage::RedundantBlobStore::requiredBytes;
    }

    MeasurementRegistry(Runtime runtime, Logger log, Measurement* const* channels, std::size_t count,
                        storage::KeyedRecordStore* store = nullptr, EventBus* events = nullptr,
                        EventId alarmEvent = 0u, WindowPool* windows = nullptr,
                        storage::ByteStorage* processing = nullptr, std::uint32_t processingOffset = 0u)
        : runtime_(runtime), log_(log), channels_(channels), count_(count), store_(store), events_(events),
          alarmEvent_(alarmEvent), windows_(windows), processing_(processing), processingOffset_(processingOffset) {}

    bool init() override;
    void run() override;

    std::size_t size() const { return count_; }
    Measurement& at(std::size_t index) { return *channels_[index]; }
    const Measurement& at(std::size_t index) const { return *channels_[index]; }
    // Index of the channel with this name, or size() if there is none.
    std::size_t find(const char* name) const;
    const WindowPool* windows() const { return windows_; }

    void acknowledgeAll();
    AlarmSummary summary() const;

    // Change and persist settings of a channel. Persisting calibration
    // needs a known sensor identity; the setting is applied either way.
    bool setCalibration(std::size_t index, const Calibration& calibration);
    bool resetCalibration(std::size_t index);
    bool settingsPersisted(std::size_t index) const;

    // Alarm rules, fault alarm, and output of a channel: applied, then stored.
    enum class RuleResult : std::uint8_t { stored, notStored, invalid, noWindow };
    RuleResult setAlarmRule(std::size_t index, std::size_t slot, const AlarmRule& rule);
    RuleResult setFaultRule(std::size_t index, const FaultRule& rule);
    RuleResult setOutputRule(std::size_t index, const OutputRule& rule);
    // Back to the defaults of the channel's MeasurementConfig.
    bool resetProcessing(std::size_t index);

    // Calibration against a reference value (thousandths of the unit) at the
    // channel's current reading. The first point sets the offset at the
    // current gain; a second point on the same channel, at least one unit
    // away, sets gain (0.5..2) and offset from both points.
    enum class CalibrationResult : std::uint8_t { stored, notStored, noReading, noFirstPoint, implausibleGain };
    CalibrationResult calibrate(std::size_t index, std::int32_t reference, bool secondPoint);

private:
    static constexpr std::size_t maxChannels = 32u;
    static void onAlarm(void* context, Measurement& channel, std::size_t rule, AlarmTransition transition);
    void loadSettings(std::size_t index);
    bool saveSettings(std::size_t index);
    void loadProcessing(std::size_t index);
    bool saveProcessing(std::size_t index, std::size_t record);
    storage::RedundantBlobStore processingRecord(std::size_t index, std::size_t record) const;
    static RuleResult result(Measurement::RuleResult applied, bool saved);
    std::size_t indexOf(const Measurement& channel) const;

    Runtime runtime_;
    Logger log_;
    Measurement* const* channels_{};
    std::size_t count_{};
    storage::KeyedRecordStore* store_{};
    EventBus* events_{};
    EventId alarmEvent_{};
    WindowPool* windows_{};
    storage::ByteStorage* processing_{};
    std::uint32_t processingOffset_{};
    bool storeReady_{};
    std::uint8_t loadedRevision_[maxChannels]{}; // identity revision whose settings are applied
    // First point of a two-point calibration.
    std::size_t pointChannel_{~std::size_t{}};
    std::int32_t pointRaw_{};
    std::int32_t pointReference_{};
};

// Formats thousandths as "12.345" (decimals 0..3), without floating point.
const char* formatMilli(char (&buffer)[20], std::int32_t value, unsigned decimals = 2u);
// Parses "12.345", "-0.5", or "12" into thousandths.
bool parseMilli(const char* text, std::int32_t& value);

} // namespace slstm32::sensors
