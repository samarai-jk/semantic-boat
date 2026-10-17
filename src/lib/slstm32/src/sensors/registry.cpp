#include "slstm32/sensors/registry.hpp"
#include <cstdio>
#include <cstring>

namespace slstm32::sensors {

namespace {

constexpr std::uint8_t settingsVersion = 1u;
constexpr std::size_t settingsBytes = 1u + 4u + 4u + 1u + 4u * Measurement::maxAlarms;

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

const char* ruleText(AlarmKind kind) {
    switch (kind) {
    case AlarmKind::high: return "above";
    case AlarmKind::low: return "below";
    case AlarmKind::riseRate: return "rising faster than";
    case AlarmKind::sensorFault: return "sensor fault";
    case AlarmKind::trendFall: return "falling by more than";
    case AlarmKind::trendRise: return "rising by more than";
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
    for (std::size_t index = 0u; index < count_ && index < maxChannels; ++index) loadSettings(index);
    log_.writef(LogLevel::info, "sensors: %u channel(s), settings slots %u of %u used", static_cast<unsigned>(count_),
                static_cast<unsigned>(store_ ? store_->used() : 0u), static_cast<unsigned>(store_ ? store_->slots() : 0u));
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
    std::uint8_t data[settingsBytes]{};
    std::size_t size = 0u;
    if (!store_->load(keyOf(channel.identity()), data, sizeof data, size) || size < settingsBytes ||
        data[0] != settingsVersion) {
        return;
    }
    const std::uint8_t* in = data + 1;
    Calibration calibration{};
    calibration.offset = get32(in);
    calibration.gainPpm = get32(in);
    const auto mask = *in++;
    channel.setCalibration(calibration);
    for (std::size_t rule = 0u; rule < Measurement::maxAlarms; ++rule) {
        const auto threshold = get32(in);
        if (mask & (1u << rule)) channel.setThreshold(rule, threshold);
    }
    log_.writef(LogLevel::info, "sensors: '%s' stored settings applied", channel.name());
}

bool MeasurementRegistry::saveSettings(std::size_t index) {
    const auto& channel = *channels_[index];
    if (!storeReady_ || !channel.identity().known()) return false;
    std::uint8_t data[settingsBytes]{settingsVersion};
    std::uint8_t* out = data + 1;
    put32(out, channel.calibration().offset);
    put32(out, channel.calibration().gainPpm);
    std::uint8_t mask = 0u;
    for (std::size_t rule = 0u; rule < channel.alarmCount(); ++rule) {
        if (channel.thresholdOverridden(rule)) mask = static_cast<std::uint8_t>(mask | (1u << rule));
    }
    *out++ = mask;
    for (std::size_t rule = 0u; rule < Measurement::maxAlarms; ++rule) {
        put32(out, rule < channel.alarmCount() ? channel.alarmRule(rule).threshold : 0);
    }
    const bool saved = store_->save(keyOf(channel.identity()), data, sizeof data);
    if (!saved) log_.writef(LogLevel::error, "sensors: '%s' settings not saved", channel.name());
    return saved;
}

bool MeasurementRegistry::settingsPersisted(std::size_t index) const {
    return index < count_ && storeReady_ && channels_[index]->identity().known();
}

bool MeasurementRegistry::setCalibration(std::size_t index, const Calibration& calibration) {
    if (index >= count_) return false;
    channels_[index]->setCalibration(calibration);
    return saveSettings(index);
}

bool MeasurementRegistry::setThreshold(std::size_t index, std::size_t rule, std::int32_t threshold) {
    if (index >= count_ || !channels_[index]->setThreshold(rule, threshold)) return false;
    return saveSettings(index);
}

bool MeasurementRegistry::resetSettings(std::size_t index) {
    if (index >= count_) return false;
    auto& channel = *channels_[index];
    channel.setCalibration({});
    for (std::size_t rule = 0u; rule < channel.alarmCount(); ++rule) {
        channel.setThreshold(rule, channel.config().alarms[rule].threshold);
    }
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
        for (std::size_t rule = 0u; rule < channel.alarmCount(); ++rule) {
            if (!channel.alarmActive(rule)) continue;
            const auto severity = channel.alarmRule(rule).severity;
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
    const auto& alarm = channel.alarmRule(rule);
    char value[20];
    char threshold[20];
    const auto unit = unitName(channel.quantity());
    const auto level = transition == AlarmTransition::activated
        ? (alarm.severity == Severity::warning ? LogLevel::warning : LogLevel::error)
        : LogLevel::info;
    if (alarm.kind == AlarmKind::sensorFault) {
        self.log_.writef(level, "alarm: %s %s %s %s (%s)", channel.name(), ruleText(alarm.kind),
                         severityName(alarm.severity), transitionName(transition),
                         channel.status() == Status::fault ? faultName(channel.fault()) : statusName(channel.status()));
    } else if (alarm.kind == AlarmKind::trendFall || alarm.kind == AlarmKind::trendRise) {
        self.log_.writef(level, "alarm: %s %s %s %s in %lu min %s %s, now %s %s in %lu min (%s)", channel.name(),
                         ruleText(alarm.kind), formatMilli(threshold, alarm.threshold), unit,
                         static_cast<unsigned long>(channel.trendWindow() / 60000u), severityName(alarm.severity),
                         transitionName(transition), formatMilli(value, channel.trend()), unit,
                         static_cast<unsigned long>(channel.trendSpanMs() / 60000u), tendencyName(channel.tendency()));
    } else {
        const bool rate = alarm.kind == AlarmKind::riseRate;
        self.log_.writef(level, "alarm: %s %s %s %s%s %s %s, now %s %s%s", channel.name(), ruleText(alarm.kind),
                         formatMilli(threshold, alarm.threshold), unit, rate ? "/min" : "",
                         severityName(alarm.severity), transitionName(transition),
                         formatMilli(value, rate ? channel.ratePerMinute() : channel.value()), unit,
                         rate ? "/min" : "");
    }
    if (self.events_) {
        const AlarmEvent event{static_cast<std::uint8_t>(self.indexOf(channel)), static_cast<std::uint8_t>(rule),
                               alarm.severity, transition, channel.value()};
        (void)self.events_->publish(self.alarmEvent_, event);
    }
}

} // namespace slstm32::sensors
