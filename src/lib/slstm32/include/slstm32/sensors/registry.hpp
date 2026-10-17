#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/event_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"
#include "slstm32/service.hpp"
#include "slstm32/storage/keyed_record_store.hpp"

namespace slstm32::sensors {

// Event payload published on every alarm transition (8 bytes).
struct AlarmEvent {
    std::uint8_t channel{};   // index in the registry
    std::uint8_t rule{};
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
// configuration (unique names, unique instance per quantity) and applies the
// settings persisted for each channel's sensor identity; channels whose
// identity becomes known later (sensors discovered on a bus) get theirs then.
// run() ticks every channel for staleness and alarm delays. Alarm
// transitions are logged and, if an event bus is given, published.
//
// Settings (calibration, threshold overrides) are stored per sensor identity
// in a KeyedRecordStore, so adding, removing, or reordering sensors never
// affects the settings of the others.
class MeasurementRegistry final : public Service {
public:
    MeasurementRegistry(Runtime runtime, Logger log, Measurement* const* channels, std::size_t count,
                        storage::KeyedRecordStore* store = nullptr, EventBus* events = nullptr,
                        EventId alarmEvent = 0u)
        : runtime_(runtime), log_(log), channels_(channels), count_(count), store_(store), events_(events),
          alarmEvent_(alarmEvent) {}

    bool init() override;
    void run() override;

    std::size_t size() const { return count_; }
    Measurement& at(std::size_t index) { return *channels_[index]; }
    const Measurement& at(std::size_t index) const { return *channels_[index]; }
    // Index of the channel with this name, or size() if there is none.
    std::size_t find(const char* name) const;

    void acknowledgeAll();
    AlarmSummary summary() const;

    // Change and persist settings of a channel. Persisting needs a known
    // sensor identity; the setting is applied either way.
    bool setCalibration(std::size_t index, const Calibration& calibration);
    bool setThreshold(std::size_t index, std::size_t rule, std::int32_t threshold);
    bool resetSettings(std::size_t index);
    bool settingsPersisted(std::size_t index) const;

private:
    static constexpr std::size_t maxChannels = 32u;
    static void onAlarm(void* context, Measurement& channel, std::size_t rule, AlarmTransition transition);
    void loadSettings(std::size_t index);
    bool saveSettings(std::size_t index);
    std::size_t indexOf(const Measurement& channel) const;

    Runtime runtime_;
    Logger log_;
    Measurement* const* channels_{};
    std::size_t count_{};
    storage::KeyedRecordStore* store_{};
    EventBus* events_{};
    EventId alarmEvent_{};
    bool storeReady_{};
    bool loaded_[maxChannels]{};
};

// Formats thousandths as "12.345" (decimals 0..3), without floating point.
const char* formatMilli(char (&buffer)[20], std::int32_t value, unsigned decimals = 2u);
// Parses "12.345", "-0.5", or "12" into thousandths.
bool parseMilli(const char* text, std::int32_t& value);

} // namespace slstm32::sensors
