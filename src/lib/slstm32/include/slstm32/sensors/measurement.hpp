#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Measurement channels: the common interface between sensor drivers and
// everything that uses measured values (alarms, displays, NMEA 2000, logs).
//
// A driver owns the hardware and pushes results into one Measurement per
// measured quantity (a combined temperature/humidity sensor feeds two). The
// channel adds what every sensor needs: validity, staleness, calibration,
// smoothing, statistics, a rate of change, and alarm rules. A
// MeasurementRegistry runs all channels of a device, persists their settings,
// and reports alarm changes.
namespace slstm32::sensors {

// Values are integers in thousandths of the unit: m°C, 0.001 %RH, mV.
enum class Quantity : std::uint8_t { temperature, relativeHumidity, voltage };

const char* quantityName(Quantity quantity);
const char* unitName(Quantity quantity);

enum class Status : std::uint8_t {
    unknown, // no result yet
    valid,
    stale,   // no new result within MeasurementConfig::staleAfterMs
    fault,   // the driver reported a failure
};

enum class Fault : std::uint8_t {
    none,
    noResponse,     // sensor absent or not answering
    checksum,       // corrupted data
    outOfRange,     // physically implausible value
    sensorReported, // the sensor flags a fault itself (open thermocouple, ...)
    notReady,       // conversion did not complete in time
};

const char* statusName(Status status);
const char* faultName(Fault fault);

// "Have a look" (warning), "act" (alarm), "act now" (emergency).
enum class Severity : std::uint8_t { warning, alarm, emergency };
const char* severityName(Severity severity);

enum class AlarmKind : std::uint8_t {
    high,        // value above threshold
    low,         // value below threshold
    riseRate,    // rise per minute above threshold (thousandths of the unit per minute)
    sensorFault, // channel faulted or stale; threshold unused
};

struct AlarmRule {
    AlarmKind kind{AlarmKind::high};
    Severity severity{Severity::warning};
    std::int32_t threshold{};
    // The alarm clears only once the value is this far back on the safe side.
    std::int32_t hysteresis{};
    // The condition must hold this long before the alarm becomes active.
    std::uint32_t delayMs{};
    // An active latching alarm stays active until acknowledged, even after
    // the condition has cleared.
    bool latching{};
    // Evaluate the smoothed instead of the latest value.
    bool useFiltered{};
};

struct MeasurementConfig {
    const char* name{};            // short and unique on the device, e.g. "coolant"
    Quantity quantity{Quantity::temperature};
    std::uint8_t instance{};       // unique per quantity on the device (NMEA 2000 instance)
    std::uint32_t staleAfterMs{10000u};
    std::uint32_t filterMs{};      // smoothing time constant; 0 = none
    const AlarmRule* alarms{};
    std::size_t alarmCount{};
};

// Stable identity of the physical sensor behind a channel, used as the key
// for its persisted settings. Set by the driver or the composition root.
struct Identity {
    enum class Kind : std::uint8_t { none, internal, i2c, oneWire, analog };
    Kind kind{Kind::none};
    std::uint64_t id{};
    std::uint8_t subchannel{}; // which quantity of a multi-quantity sensor (0..15)

    static constexpr Identity internal(std::uint8_t index, std::uint8_t subchannel = 0u) {
        return {Kind::internal, static_cast<std::uint64_t>(index) + 1u, subchannel};
    }
    static constexpr Identity i2c(std::uint8_t bus, std::uint8_t muxChannel, std::uint8_t address,
                                  std::uint8_t subchannel = 0u) {
        return {Kind::i2c,
                (static_cast<std::uint64_t>(bus) << 16u) | (static_cast<std::uint64_t>(muxChannel) << 8u) |
                    address | (1ull << 32u),
                subchannel};
    }
    static constexpr Identity oneWire(std::uint64_t rom, std::uint8_t subchannel = 0u) {
        return {Kind::oneWire, rom, subchannel};
    }
    static constexpr Identity analog(std::uint8_t input, std::uint8_t subchannel = 0u) {
        return {Kind::analog, static_cast<std::uint64_t>(input) + 1u, subchannel};
    }
    bool known() const { return kind != Kind::none && id != 0u; }
};

// value = raw * gainPpm / 1e6 + offset
struct Calibration {
    std::int32_t offset{};
    std::int32_t gainPpm{1000000};
};

enum class AlarmTransition : std::uint8_t { activated, cleared, acknowledged };

class Measurement;
using AlarmListener = void (*)(void* context, Measurement& channel, std::size_t rule, AlarmTransition transition);

class Measurement {
public:
    static constexpr std::size_t maxAlarms = 4u;

    explicit Measurement(const MeasurementConfig& config);

    // --- Driver side ---
    void setIdentity(const Identity& identity) { identity_ = identity; }
    void update(std::int32_t raw, std::uint32_t now);
    void fail(Fault fault, std::uint32_t now);

    // --- Readers ---
    const MeasurementConfig& config() const { return config_; }
    const char* name() const { return config_.name; }
    Quantity quantity() const { return config_.quantity; }
    const Identity& identity() const { return identity_; }
    Status status() const { return status_; }
    bool valid() const { return status_ == Status::valid; }
    Fault fault() const { return fault_; }
    std::int32_t value() const { return value_; }       // calibrated latest value
    std::int32_t filtered() const { return filtered_; } // smoothed (equals value without smoothing)
    std::int32_t raw() const { return raw_; }
    std::uint32_t lastUpdateAt() const { return lastUpdateAt_; }
    std::uint32_t samples() const { return samples_; }
    std::int32_t minimum() const { return minimum_; }
    std::int32_t maximum() const { return maximum_; }
    bool rateValid() const { return rateValid_; }
    std::int32_t ratePerMinute() const { return ratePerMinute_; }

    std::size_t alarmCount() const { return alarmCount_; }
    const AlarmRule& alarmRule(std::size_t index) const { return rules_[index]; }
    bool alarmActive(std::size_t index) const { return alarms_[index].active; }
    bool alarmAcknowledged(std::size_t index) const { return alarms_[index].acknowledged; }

    // --- Settings (persisted by MeasurementRegistry) ---
    const Calibration& calibration() const { return calibration_; }
    void setCalibration(const Calibration& calibration);
    bool setThreshold(std::size_t rule, std::int32_t threshold);
    bool thresholdOverridden(std::size_t rule) const { return rule < alarmCount_ && rules_[rule].threshold != config_.alarms[rule].threshold; }

    // --- Registry side ---
    void setAlarmListener(AlarmListener listener, void* context) {
        listener_ = listener;
        listenerContext_ = context;
    }
    // Staleness and alarm delays; called regularly by the registry.
    void tick(std::uint32_t now);
    // Acknowledges active alarms; latching alarms whose condition has cleared end.
    void acknowledge();
    void resetStatistics();

private:
    struct AlarmState {
        bool pending{};
        bool active{};
        bool acknowledged{};
        bool conditionCleared{};
        std::uint32_t pendingSince{};
    };
    struct RatePoint {
        std::uint32_t at{};
        std::int32_t value{};
    };
    static constexpr std::size_t ratePoints = 8u;
    static constexpr std::uint32_t rateStepMs = 7500u; // 8 points span one minute
    static constexpr std::uint32_t rateMinimumSpanMs = 15000u;

    void evaluate(std::uint32_t now);
    bool condition(const AlarmRule& rule, bool active) const;
    void notify(std::size_t rule, AlarmTransition transition);
    void trackRate(std::uint32_t now);

    const MeasurementConfig& config_;
    std::array<AlarmRule, maxAlarms> rules_{};
    std::array<AlarmState, maxAlarms> alarms_{};
    std::size_t alarmCount_{};
    Identity identity_{};
    Calibration calibration_{};
    Status status_{Status::unknown};
    Fault fault_{Fault::none};
    std::int32_t raw_{};
    std::int32_t value_{};
    std::int32_t filtered_{};
    std::uint32_t lastUpdateAt_{};
    std::uint32_t firstTickAt_{};
    bool ticked_{};
    std::uint32_t samples_{};
    std::int32_t minimum_{};
    std::int32_t maximum_{};
    std::array<RatePoint, ratePoints> rate_{};
    std::size_t rateHead_{};
    std::size_t rateCount_{};
    std::int32_t ratePerMinute_{};
    bool rateValid_{};
    AlarmListener listener_{};
    void* listenerContext_{};
};

} // namespace slstm32::sensors
