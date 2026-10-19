#include "slstm32/sensors/registry.hpp"
#include <cstdio>
#include <cstring>

namespace slstm32::sensors {

namespace {

// Sensor settings: version, calibration offset and gain (version 1 records
// carried alarm thresholds after them, which are ignored now).
constexpr std::uint8_t settingsVersion = 2u;
constexpr std::size_t settingsBytes = 1u + 4u + 4u;
// Processing records: version, name hash, the rule structs as they are in
// memory (record 0: output and fault alarm; records 1..4: alarm rules
// 0..3). Only this firmware's own code reads them; a record of another size
// is ignored. Bump the version when a struct keeps its size but changes its
// layout. Version 0 = not set.
constexpr std::uint8_t processingVersion = 1u;
constexpr std::size_t processingHeader = 1u + 4u;
struct OutputAndFault {
    OutputRule output;
    FaultRule fault;
};
constexpr std::size_t processingCapacity =
    processingHeader + (sizeof(AlarmRule) > sizeof(OutputAndFault) ? sizeof(AlarmRule) : sizeof(OutputAndFault));
static_assert(processingCapacity <= storage::RedundantBlobStore::maxPayloadBytes, "rules fit a record");

// FNV-1a: ties a processing record to its channel's name, so a firmware
// that reorders its channels never applies one channel's alarms to another.
std::uint32_t nameHash(const char* name) {
    std::uint32_t hash = 2166136261u;
    for (; name && *name != '\0'; ++name) hash = (hash ^ static_cast<std::uint8_t>(*name)) * 16777619u;
    return hash;
}

void put32(std::uint8_t*& out, std::int32_t value) {
    const auto bits = static_cast<std::uint32_t>(value);
    for (unsigned index = 0; index < 4u; ++index) *out++ = static_cast<std::uint8_t>(bits >> (8u * index));
}

std::int32_t get32(const std::uint8_t*& in) {
    std::uint32_t bits = 0u;
    for (unsigned index = 0; index < 4u; ++index) bits |= static_cast<std::uint32_t>(*in++) << (8u * index);
    return static_cast<std::int32_t>(bits);
}

storage::KeyedRecordStore::Key keyOf(const Identity& identity) {
    return {identity.id, static_cast<std::uint8_t>(static_cast<unsigned>(identity.kind) << 4u |
                                                   (identity.subchannel & 0x0Fu))};
}

const char* transitionName(AlarmTransition transition) {
    switch (transition) {
    case AlarmTransition::activated: return "ACTIVE";
    case AlarmTransition::cleared: return "cleared";
    case AlarmTransition::acknowledged: return "acknowledged";
    }
    return "?";
}

} // namespace

const char* formatMilli(char (&buffer)[20], std::int32_t value, unsigned decimals) {
    if (decimals > 3u) decimals = 3u;
    long divisor = 1000;
    long scaled = value;
    for (unsigned step = 3u; step > decimals; --step) {
        scaled = (scaled >= 0 ? scaled + 5 : scaled - 5) / 10;
        divisor /= 10;
    }
    const long magnitude = scaled < 0 ? -scaled : scaled;
    if (decimals == 0u) {
        std::snprintf(buffer, sizeof buffer, "%s%ld", scaled < 0 ? "-" : "", magnitude);
    } else {
        // The fraction is below 1000; the narrow type tells the compiler so.
        std::snprintf(buffer, sizeof buffer, "%s%ld.%0*u", scaled < 0 ? "-" : "", magnitude / divisor,
                      static_cast<int>(decimals), static_cast<unsigned>(static_cast<std::uint16_t>(magnitude % divisor)));
    }
    return buffer;
}

bool parseMilli(const char* text, std::int32_t& value) {
    bool negative = false;
    if (*text == '-' || *text == '+') negative = *text++ == '-';
    if (*text < '0' || *text > '9') return false;
    std::int32_t whole = 0;
    while (*text >= '0' && *text <= '9') {
        whole = whole * 10 + (*text++ - '0');
        if (whole > 2000000) return false;
    }
    std::int32_t fraction = 0;
    int digits = 0;
    if (*text == '.') {
        ++text;
        while (*text >= '0' && *text <= '9') {
            if (digits < 3) {
                fraction = fraction * 10 + (*text - '0');
                ++digits;
            }
            ++text;
        }
    }
    if (*text != '\0') return false;
    while (digits++ < 3) fraction *= 10;
    value = (whole * 1000 + fraction) * (negative ? -1 : 1);
    return true;
}

bool MeasurementRegistry::init() {
    bool ok = count_ <= maxChannels;
    for (std::size_t first = 0u; first < count_ && ok; ++first) {
        const auto& a = channels_[first]->config();
        for (std::size_t second = first + 1u; second < count_; ++second) {
            const auto& b = channels_[second]->config();
            if (a.name && b.name && std::strcmp(a.name, b.name) == 0) {
                log_.writef(LogLevel::error, "sensors: name '%s' used twice", a.name);
                ok = false;
            }
            if (a.quantity == b.quantity && a.instance == b.instance) {
                log_.writef(LogLevel::error, "sensors: %s instance %u used by '%s' and '%s'",
                            quantityName(a.quantity), static_cast<unsigned>(a.instance), a.name, b.name);
                ok = false;
            }
        }
    }
    for (std::size_t index = 0u; index < count_ && index < maxChannels; ++index) {
        channels_[index]->setAlarmListener(&onAlarm, this);
    }
    storeReady_ = store_ && store_->scan();
    if (store_ && !storeReady_) log_.write(LogLevel::error, "sensors: settings store not readable");
    for (std::size_t index = 0u; index < count_ && index < maxChannels; ++index) {
        loadSettings(index);
        loadProcessing(index);
    }
    log_.writef(LogLevel::info, "sensors: %u channel(s), settings slots %u of %u used, windows %u of %u free",
                static_cast<unsigned>(count_), static_cast<unsigned>(store_ ? store_->used() : 0u),
                static_cast<unsigned>(store_ ? store_->slots() : 0u),
                static_cast<unsigned>(windows_ ? windows_->available() : 0u),
                static_cast<unsigned>(windows_ ? windows_->size() : 0u));
    return ok;
}

void MeasurementRegistry::run() {
    const auto now = runtime_.millis();
    for (std::size_t index = 0u; index < count_ && index < maxChannels; ++index) {
        if (loadedRevision_[index] != channels_[index]->identityRevision()) loadSettings(index);
        channels_[index]->tick(now);
    }
}

std::size_t MeasurementRegistry::find(const char* name) const {
    for (std::size_t index = 0u; index < count_; ++index) {
        if (name && std::strcmp(channels_[index]->name(), name) == 0) return index;
    }
    return count_;
}

std::size_t MeasurementRegistry::indexOf(const Measurement& channel) const {
    for (std::size_t index = 0u; index < count_; ++index) {
        if (channels_[index] == &channel) return index;
    }
    return count_;
}

void MeasurementRegistry::loadSettings(std::size_t index) {
    auto& channel = *channels_[index];
    loadedRevision_[index] = channel.identityRevision();
    if (!channel.identity().known() || !storeReady_) return;
    std::uint8_t data[storage::KeyedRecordStore::maxPayloadBytes]{};
    std::size_t size = 0u;
    if (!store_->load(keyOf(channel.identity()), data, sizeof data, size) || size < settingsBytes ||
        (data[0] != settingsVersion && data[0] != 1u)) {
        return;
    }
    const std::uint8_t* in = data + 1;
    Calibration calibration{};
    calibration.offset = get32(in);
    calibration.gainPpm = get32(in);
    channel.setCalibration(calibration);
    log_.writef(LogLevel::info, "sensors: '%s' stored calibration applied", channel.name());
}

bool MeasurementRegistry::saveSettings(std::size_t index) {
    const auto& channel = *channels_[index];
    if (!storeReady_ || !channel.identity().known()) return false;
    std::uint8_t data[settingsBytes]{settingsVersion};
    std::uint8_t* out = data + 1;
    put32(out, channel.calibration().offset);
    put32(out, channel.calibration().gainPpm);
    const bool saved = store_->save(keyOf(channel.identity()), data, sizeof data);
    if (!saved) log_.writef(LogLevel::error, "sensors: '%s' settings not saved", channel.name());
    return saved;
}

storage::RedundantBlobStore MeasurementRegistry::processingRecord(std::size_t index, std::size_t record) const {
    const auto offset = processingOffset_ + static_cast<std::uint32_t>((index * processingRecords + record) *
                                                                       storage::RedundantBlobStore::requiredBytes);
    return storage::RedundantBlobStore{*processing_, offset, storage::RedundantBlobStore::requiredBytes};
}

void MeasurementRegistry::loadProcessing(std::size_t index) {
    auto& channel = *channels_[index];
    const auto& config = channel.config();
    const auto now = runtime_.millis();
    const auto hash = nameHash(channel.name());
    bool stored = false;
    for (std::size_t record = 0u; record < processingRecords; ++record) {
        const std::size_t content = record == 0u ? sizeof(OutputAndFault) : sizeof(AlarmRule);
        std::uint8_t data[processingCapacity]{};
        std::size_t size = 0u;
        std::uint32_t storedHash = 0u;
        if (processing_ && processingRecord(index, record).load(data, sizeof data, size) &&
            size == processingHeader + content && data[0] == processingVersion) {
            std::memcpy(&storedHash, data + 1, sizeof storedHash);
        }
        const bool found = storedHash == hash && size == processingHeader + content;
        stored = stored || found;
        Measurement::RuleResult applied{};
        if (record == 0u) {
            OutputAndFault rules{config.output, config.fault};
            if (found) std::memcpy(&rules, data + processingHeader, sizeof rules);
            applied = channel.setOutputRule(rules.output, windows_, now);
            if (applied == Measurement::RuleResult::applied) applied = channel.setFaultRule(rules.fault);
        } else {
            const auto slot = record - 1u;
            AlarmRule rule = slot < config.alarmCount && config.alarms ? config.alarms[slot] : AlarmRule{};
            if (found) std::memcpy(&rule, data + processingHeader, sizeof rule);
            applied = channel.setAlarmRule(slot, rule, windows_, now);
        }
        if (applied != Measurement::RuleResult::applied) {
            log_.writef(LogLevel::error, "sensors: '%s' %s %u not applied (%s)", channel.name(),
                        record == 0u ? "output/fault" : "alarm", static_cast<unsigned>(record == 0u ? 0u : record - 1u),
                        applied == Measurement::RuleResult::noWindow ? "no window left" : "invalid");
        }
    }
    if (stored) log_.writef(LogLevel::info, "sensors: '%s' stored alarms/output applied", channel.name());
}

bool MeasurementRegistry::saveProcessing(std::size_t index, std::size_t record) {
    if (!processing_) return false;
    const auto& channel = *channels_[index];
    std::uint8_t data[processingCapacity]{processingVersion};
    const auto hash = nameHash(channel.name());
    std::memcpy(data + 1, &hash, sizeof hash);
    std::size_t size = processingHeader;
    if (record == 0u) {
        const OutputAndFault rules{channel.outputRule(), channel.faultRule()};
        std::memcpy(data + size, &rules, sizeof rules);
        size += sizeof rules;
    } else {
        std::memcpy(data + size, &channel.alarmRule(record - 1u), sizeof(AlarmRule));
        size += sizeof(AlarmRule);
    }
    const bool saved = processingRecord(index, record).commit(data, size);
    if (!saved) log_.writef(LogLevel::error, "sensors: '%s' alarms/output not saved", channel.name());
    return saved;
}

MeasurementRegistry::RuleResult MeasurementRegistry::result(Measurement::RuleResult applied, bool saved) {
    switch (applied) {
    case Measurement::RuleResult::applied: return saved ? RuleResult::stored : RuleResult::notStored;
    case Measurement::RuleResult::invalid: return RuleResult::invalid;
    case Measurement::RuleResult::noWindow: return RuleResult::noWindow;
    }
    return RuleResult::invalid;
}

MeasurementRegistry::RuleResult MeasurementRegistry::setAlarmRule(std::size_t index, std::size_t slot,
                                                                  const AlarmRule& rule) {
    if (index >= count_ || slot >= Measurement::maxAlarms) return RuleResult::invalid;
    const auto applied = channels_[index]->setAlarmRule(slot, rule, windows_, runtime_.millis());
    return result(applied, applied == Measurement::RuleResult::applied && saveProcessing(index, 1u + slot));
}

MeasurementRegistry::RuleResult MeasurementRegistry::setFaultRule(std::size_t index, const FaultRule& rule) {
    if (index >= count_) return RuleResult::invalid;
    const auto applied = channels_[index]->setFaultRule(rule);
    return result(applied, applied == Measurement::RuleResult::applied && saveProcessing(index, 0u));
}

MeasurementRegistry::RuleResult MeasurementRegistry::setOutputRule(std::size_t index, const OutputRule& rule) {
    if (index >= count_) return RuleResult::invalid;
    const auto applied = channels_[index]->setOutputRule(rule, windows_, runtime_.millis());
    return result(applied, applied == Measurement::RuleResult::applied && saveProcessing(index, 0u));
}

bool MeasurementRegistry::resetProcessing(std::size_t index) {
    if (index >= count_) return false;
    bool erased = processing_ != nullptr;
    for (std::size_t record = 0u; record < processingRecords && processing_; ++record) {
        const std::uint8_t unset[1]{0u};
        erased = processingRecord(index, record).commit(unset, sizeof unset) && erased;
    }
    loadProcessing(index);
    return erased;
}

bool MeasurementRegistry::settingsPersisted(std::size_t index) const {
    return index < count_ && storeReady_ && channels_[index]->identity().known();
}

bool MeasurementRegistry::setCalibration(std::size_t index, const Calibration& calibration) {
    if (index >= count_) return false;
    channels_[index]->setCalibration(calibration);
    return saveSettings(index);
}

MeasurementRegistry::CalibrationResult MeasurementRegistry::calibrate(std::size_t index, std::int32_t reference,
                                                                     bool secondPoint) {
    if (index >= count_ || !channels_[index]->valid()) return CalibrationResult::noReading;
    auto& channel = *channels_[index];
    const auto raw = channel.raw();
    Calibration calibration = channel.calibration();
    if (secondPoint) {
        const auto rawSpan = static_cast<std::int64_t>(raw) - pointRaw_;
        if (pointChannel_ != index || (rawSpan < 1000 && rawSpan > -1000)) return CalibrationResult::noFirstPoint;
        const auto gain = (static_cast<std::int64_t>(reference) - pointReference_) * 1000000 / rawSpan;
        if (gain < 500000 || gain > 2000000) return CalibrationResult::implausibleGain;
        calibration.gainPpm = static_cast<std::int32_t>(gain);
        calibration.offset = static_cast<std::int32_t>(pointReference_ - static_cast<std::int64_t>(pointRaw_) * gain / 1000000);
        pointChannel_ = ~std::size_t{};
    } else {
        calibration.offset = static_cast<std::int32_t>(reference - static_cast<std::int64_t>(raw) * calibration.gainPpm / 1000000);
        pointChannel_ = index;
        pointRaw_ = raw;
        pointReference_ = reference;
    }
    return setCalibration(index, calibration) ? CalibrationResult::stored : CalibrationResult::notStored;
}

bool MeasurementRegistry::resetCalibration(std::size_t index) {
    if (index >= count_) return false;
    auto& channel = *channels_[index];
    channel.setCalibration({});
    if (!storeReady_ || !channel.identity().known()) return false;
    return store_->erase(keyOf(channel.identity()));
}

void MeasurementRegistry::acknowledgeAll() {
    for (std::size_t index = 0u; index < count_; ++index) channels_[index]->acknowledge();
}

AlarmSummary MeasurementRegistry::summary() const {
    AlarmSummary result{};
    for (std::size_t index = 0u; index < count_; ++index) {
        const auto& channel = *channels_[index];
        for (std::size_t rule = 0u; rule < Measurement::alarmSlots; ++rule) {
            if (!channel.alarmActive(rule)) continue;
            const auto severity = channel.alarmSeverity(rule);
            if (!result.active || severity > result.highest) result.highest = severity;
            result.active = true;
            if (!channel.alarmAcknowledged(rule)) {
                if (!result.unacknowledged || severity > result.highestUnacknowledged) {
                    result.highestUnacknowledged = severity;
                }
                result.unacknowledged = true;
            }
        }
    }
    return result;
}

void MeasurementRegistry::onAlarm(void* context, Measurement& channel, std::size_t rule, AlarmTransition transition) {
    auto& self = *static_cast<MeasurementRegistry*>(context);
    const auto severity = channel.alarmSeverity(rule);
    const auto level = transition == AlarmTransition::activated
        ? (severity == Severity::warning ? LogLevel::warning : LogLevel::error)
        : LogLevel::info;
    if (rule == Measurement::faultSlot) {
        self.log_.writef(level, "alarm: %s sensor fault %s %s (%s)", channel.name(), severityName(severity),
                         transitionName(transition),
                         channel.status() == Status::fault ? faultName(channel.fault()) : statusName(channel.status()));
    } else {
        const auto& alarm = channel.alarmRule(rule);
        char value[20];
        char threshold[20];
        char upper[20];
        std::int32_t now = 0;
        const bool known = channel.functionValue(alarm.function, channel.alarmWindow(rule), now);
        const bool band = alarm.comparison == Comparison::inside || alarm.comparison == Comparison::outside;
        const auto unit = unitName(channel.quantity());
        const char* perMinute = alarm.function == ValueFunction::rate ? "/min" : "";
        self.log_.writef(level, "alarm: %s %u %s %s %s%s%s %s%s %s %s, now %s", channel.name(), static_cast<unsigned>(rule),
                         functionName(alarm.function), comparisonName(alarm.comparison),
                         formatMilli(threshold, alarm.threshold), band ? ".." : "", band ? formatMilli(upper, alarm.upper) : "",
                         unit, perMinute, severityName(severity), transitionName(transition),
                         known ? formatMilli(value, now) : "-");
    }
    if (self.events_) {
        const AlarmEvent event{static_cast<std::uint8_t>(self.indexOf(channel)), static_cast<std::uint8_t>(rule),
                               severity, transition, channel.value()};
        (void)self.events_->publish(self.alarmEvent_, event);
    }
}

} // namespace slstm32::sensors
