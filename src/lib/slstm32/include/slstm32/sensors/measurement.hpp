#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "slstm32/sensors/window.hpp"

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

// Values are integers in thousandths of the unit: m°C, 0.001 %RH, mV, mlx,
// thousandths of the UV index, 0.001 hPa (= 0.1 Pa), thousandths of a degree,
// thousandths of a ppm (gas concentration), thousandths of an rpm.
enum class Quantity : std::uint8_t {
    temperature,
    relativeHumidity,
    voltage,
    illuminance,
    uvIndex,
    pressure,
    angle,
    concentration,
    rotationalSpeed,
};

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

enum class Comparison : std::uint8_t {
    above,   // value > threshold
    atLeast, // value >= threshold
    below,   // value < threshold
    atMost,  // value <= threshold
    inside,  // threshold <= value <= upper
    outside, // value < threshold or value > upper
};
const char* comparisonName(Comparison comparison);

// An alarm on a value of the channel, e.g. "warning when the average over
// 60 s is below 11.8 V for 30 s". Thresholds are in the unit of the value:
// thousandths of the channel unit (a difference for change and range);
// thousandths per minute for rate.
struct AlarmRule {
    ValueFunction function{ValueFunction::none}; // none: the slot is unused
    Comparison comparison{Comparison::above};
    Severity severity{Severity::warning};
    // An active latching alarm stays active until acknowledged, even after
    // the condition has cleared.
    bool latching{};
    WindowLength window{};        // for average, minimum, maximum, change
    std::uint16_t delaySeconds{}; // the condition must hold this long; 0 = at the first sample
    std::int32_t threshold{};     // lower limit for inside/outside
    std::int32_t upper{};         // upper limit for inside/outside
    // The alarm clears only once the value is this far back on the safe side.
    std::int32_t hysteresis{};
};

// The alarm when the channel has no valid value (sensor fault or stale).
struct FaultRule {
    bool enabled{};
    Severity severity{Severity::warning};
    std::uint16_t delaySeconds{};
    bool latching{};
};

// How the channel's output value (published, shown) is derived: last,
// average, minimum, maximum, or median (readings, not differences).
struct OutputRule {
    ValueFunction function{ValueFunction::last};
    WindowLength window{};
};

struct MeasurementConfig {
    const char* name{};            // short and unique on the device, e.g. "coolant"
    Quantity quantity{Quantity::temperature};
    std::uint8_t instance{};       // unique per quantity on the device (NMEA 2000 instance)
    std::uint32_t staleAfterMs{10000u};
    std::uint32_t filterMs{};      // smoothing time constant of the rate and trend; 0 = none
    // Default alarm rules (up to Measurement::maxAlarms), fault alarm, and
    // output; settings stored for the channel replace them.
    const AlarmRule* alarms{};
    std::size_t alarmCount{};
    FaultRule fault{};
    OutputRule output{};
    // Slow trend, e.g. the 3-hour pressure tendency: the change over this
    // window, sampled into a buffer given with Measurement::setTrendBuffer().
    // 0 = no trend.
    std::uint32_t trendWindowMs{};
    // Tendency labels: a change below trendSteady is "steady", above
    // trendRapid "rising/falling rapidly" (0 = no rapid label).
    std::int32_t trendSteady{};
    std::int32_t trendRapid{};
    // A direction that wraps at 360 degrees (heading, wind direction; not
    // pitch, roll, or rudder): averages are taken across 0/360, and only the
    // functions last and average are allowed.
    bool circular{};
};

struct TrendPoint {
    std::uint32_t at{};
    std::int32_t value{};
};

enum class Tendency : std::uint8_t { unknown, steady, rising, falling, risingRapidly, fallingRapidly };
const char* tendencyName(Tendency tendency);

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
    // Alarm slots: the rules 0..maxAlarms-1 and the fault alarm.
    static constexpr std::size_t faultSlot = maxAlarms;
    static constexpr std::size_t alarmSlots = maxAlarms + 1u;

    explicit Measurement(const MeasurementConfig& config);

    // --- Driver side ---
    // A different identity (another sensor on the same input) resets the
    // calibration and statistics; the registry then applies the settings
    // stored for the new sensor. Alarms and output belong to the channel and
    // stay.
    void setIdentity(const Identity& identity);
    void update(std::int32_t raw, std::uint32_t now);
    void fail(Fault fault, std::uint32_t now);
    // The channel's raw value is the latest calibrated sample unless the
    // driver sets something that suits its sensor better, after update().
    void setRawValue(std::int32_t value) { rawValue_ = value; }

    // --- Readers ---
    const MeasurementConfig& config() const { return config_; }
    const char* name() const { return config_.name; }
    Quantity quantity() const { return config_.quantity; }
    const Identity& identity() const { return identity_; }
    // Incremented on every identity change.
    std::uint8_t identityRevision() const { return identityRevision_; }
    Status status() const { return status_; }
    bool valid() const { return status_ == Status::valid; }
    Fault fault() const { return fault_; }
    std::int32_t value() const { return value_; }       // calibrated latest value
    std::int32_t filtered() const { return filtered_; } // smoothed (equals value without smoothing)
    std::int32_t raw() const { return raw_; }           // uncalibrated sensor reading
    std::int32_t rawValue() const { return rawValue_; } // see setRawValue()
    // The output value per the output rule; false without a valid value.
    bool output(std::int32_t& value) const;
    // The value of a function as alarms see it; false if there is none.
    bool functionValue(ValueFunction function, const Window* window, std::int32_t& value) const;
    std::uint32_t lastUpdateAt() const { return lastUpdateAt_; }
    std::uint32_t samples() const { return samples_; }
    std::int32_t minimum() const { return minimum_; }
    std::int32_t maximum() const { return maximum_; }
    bool rateValid() const { return rateValid_; }
    std::int32_t ratePerMinute() const { return ratePerMinute_; }

    // --- Trend (see MeasurementConfig::trendWindowMs) ---
    // The buffer holds the samples of one window: one every window / (count - 1).
    void setTrendBuffer(TrendPoint* points, std::size_t count);
    // Changes the window at run time (tests, service); restarts the trend.
    void setTrendWindow(std::uint32_t windowMs);
    std::uint32_t trendWindow() const { return trendWindowMs_; }
    // Change of the smoothed value over the covered span; valid once two
    // samples are stored. After a start or a gap the span is shorter than the
    // window until the buffer has filled.
    bool trendValid() const { return trendValid_; }
    std::int32_t trend() const { return trendChange_; }
    std::uint32_t trendSpanMs() const { return trendSpanMs_; }
    Tendency tendency() const;

    // --- Alarms and output (slots 0..maxAlarms-1 rules, faultSlot) ---
    const AlarmRule& alarmRule(std::size_t slot) const { return rules_[slot]; }
    const FaultRule& faultRule() const { return faultRule_; }
    const OutputRule& outputRule() const { return output_; }
    const Window* alarmWindow(std::size_t slot) const { return windows_[slot]; }
    bool alarmUsed(std::size_t slot) const;
    Severity alarmSeverity(std::size_t slot) const;
    bool alarmActive(std::size_t slot) const { return alarms_[slot].active; }
    bool alarmAcknowledged(std::size_t slot) const { return alarms_[slot].acknowledged; }

    // Rule changes take windows from the pool (and return unused ones). An
    // active alarm of a changed slot clears. Nothing changes for an invalid
    // rule or when the pool has no window left.
    enum class RuleResult : std::uint8_t { applied, invalid, noWindow };
    RuleResult setAlarmRule(std::size_t slot, const AlarmRule& rule, WindowPool* pool, std::uint32_t now);
    RuleResult setFaultRule(const FaultRule& rule);
    RuleResult setOutputRule(const OutputRule& rule, WindowPool* pool, std::uint32_t now);
    static bool validRule(const AlarmRule& rule);
    static bool validRule(const OutputRule& rule);
    // Whether the channel allows the function (circular channels: last, average).
    bool allows(ValueFunction function) const;

    // --- Settings (persisted by MeasurementRegistry) ---
    const Calibration& calibration() const { return calibration_; }
    void setCalibration(const Calibration& calibration);

    // --- Registry side ---
    void setAlarmListener(AlarmListener listener, void* context) {
        listener_ = listener;
        listenerContext_ = context;
    }
    // Staleness, windows, and alarm delays; called regularly by the registry.
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
    void evaluateSlot(std::size_t slot, bool holds, std::uint32_t delayMs, bool latching, std::uint32_t now);
    bool condition(std::size_t slot, bool active, bool& known) const;
    void clearSlot(std::size_t slot);
    RuleResult setWindow(Window*& window, ValueFunction function, WindowLength length, WindowPool* pool,
                         std::uint32_t now);
    void notify(std::size_t slot, AlarmTransition transition);
    void trackRate(std::uint32_t now);
    void trackTrend(std::uint32_t now);
    void resetTrend();

    const MeasurementConfig& config_;
    std::array<AlarmRule, maxAlarms> rules_{};
    FaultRule faultRule_{};
    OutputRule output_{};
    std::array<AlarmState, alarmSlots> alarms_{};
    std::array<Window*, maxAlarms> windows_{};
    Window* outputWindow_{};
    Identity identity_{};
    std::uint8_t identityRevision_{};
    Calibration calibration_{};
    Status status_{Status::unknown};
    Fault fault_{Fault::none};
    std::int32_t raw_{};
    std::int32_t value_{};
    std::int32_t rawValue_{};
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
    TrendPoint* trendPoints_{};
    std::size_t trendCapacity_{};
    std::size_t trendHead_{};
    std::size_t trendCount_{};
    std::uint32_t trendWindowMs_{};
    std::int32_t trendChange_{};
    std::uint32_t trendSpanMs_{};
    bool trendValid_{};
    AlarmListener listener_{};
    void* listenerContext_{};
};

} // namespace slstm32::sensors
