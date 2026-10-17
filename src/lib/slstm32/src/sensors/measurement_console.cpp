#include "slstm32/sensors/measurement_console.hpp"
#include <cstdlib>
#include <cstring>

namespace slstm32::sensors {

namespace {

const char* kindName(AlarmKind kind) {
    switch (kind) {
    case AlarmKind::high: return "high";
    case AlarmKind::low: return "low";
    case AlarmKind::riseRate: return "rise";
    case AlarmKind::sensorFault: return "fault";
    }
    return "?";
}

const char* identityName(Identity::Kind kind) {
    switch (kind) {
    case Identity::Kind::none: return "none";
    case Identity::Kind::internal: return "internal";
    case Identity::Kind::i2c: return "i2c";
    case Identity::Kind::oneWire: return "1-wire";
    case Identity::Kind::analog: return "analog";
    }
    return "?";
}

} // namespace

bool MeasurementConsole::registerCommands(CommandConsole& console) {
    return console.add({"sensors", "sensors  list measurement channels", &sensorsCommand, this}) &&
           console.add({"sensor", "sensor <name> [cal|cal2 <ref> | alarm <rule> <value> | reset | stats]",
                        &sensorCommand, this}) &&
           console.add({"ack", "ack  acknowledge all alarms", &ackCommand, this});
}

void MeasurementConsole::list() const {
    char value[20];
    char minimum[20];
    char maximum[20];
    for (std::size_t index = 0u; index < registry_.size(); ++index) {
        const auto& channel = registry_.at(index);
        unsigned active = 0u;
        for (std::size_t rule = 0u; rule < channel.alarmCount(); ++rule) active += channel.alarmActive(rule) ? 1u : 0u;
        const auto unit = unitName(channel.quantity());
        if (channel.samples() == 0u) {
            log_.writef(LogLevel::info, "  %-10s %s %u  %s", channel.name(), quantityName(channel.quantity()),
                        static_cast<unsigned>(channel.config().instance), statusName(channel.status()));
            continue;
        }
        log_.writef(LogLevel::info, "  %-10s %s %u  %s %s %s (%s..%s)%s%s", channel.name(),
                    quantityName(channel.quantity()), static_cast<unsigned>(channel.config().instance),
                    channel.status() == Status::fault ? faultName(channel.fault()) : statusName(channel.status()),
                    formatMilli(value, channel.value()), unit, formatMilli(minimum, channel.minimum()),
                    formatMilli(maximum, channel.maximum()), active ? ", alarms active: " : "",
                    active ? (active == 1u ? "1" : "several") : "");
    }
    if (registry_.size() == 0u) log_.write(LogLevel::info, "  no measurement channels");
}

void MeasurementConsole::detail(std::size_t index) const {
    const auto& channel = registry_.at(index);
    const auto unit = unitName(channel.quantity());
    char a[20];
    char b[20];
    char c[20];
    const auto& identity = channel.identity();
    log_.writef(LogLevel::info, "%s: %s instance %u, sensor %s %08lX%08lX/%u, settings %s", channel.name(),
                quantityName(channel.quantity()), static_cast<unsigned>(channel.config().instance),
                identityName(identity.kind), static_cast<unsigned long>(identity.id >> 32u),
                static_cast<unsigned long>(identity.id & 0xFFFFFFFFu), static_cast<unsigned>(identity.subchannel),
                registry_.settingsPersisted(index) ? "persistent" : "not persistent");
    log_.writef(LogLevel::info, "  %s, %lu samples, value %s %s, filtered %s, raw %s",
                channel.status() == Status::fault ? faultName(channel.fault()) : statusName(channel.status()),
                static_cast<unsigned long>(channel.samples()), formatMilli(a, channel.value(), 3u), unit,
                formatMilli(b, channel.filtered(), 3u), formatMilli(c, channel.raw(), 3u));
    log_.writef(LogLevel::info, "  min %s, max %s, rate %s %s/min", formatMilli(a, channel.minimum()),
                formatMilli(b, channel.maximum()),
                channel.rateValid() ? formatMilli(c, channel.ratePerMinute()) : "-", unit);
    log_.writef(LogLevel::info, "  calibration offset %s %s, gain %ld ppm", formatMilli(a, channel.calibration().offset, 3u),
                unit, static_cast<long>(channel.calibration().gainPpm));
    for (std::size_t rule = 0u; rule < channel.alarmCount(); ++rule) {
        const auto& alarm = channel.alarmRule(rule);
        const char* state = !channel.alarmActive(rule) ? "ok" : channel.alarmAcknowledged(rule) ? "active, acknowledged" : "ACTIVE";
        if (alarm.kind == AlarmKind::sensorFault) {
            log_.writef(LogLevel::info, "  alarm %u: fault %s, delay %lu ms: %s", static_cast<unsigned>(rule),
                        severityName(alarm.severity), static_cast<unsigned long>(alarm.delayMs), state);
        } else {
            log_.writef(LogLevel::info, "  alarm %u: %s %s %s%s %s (hyst %s), delay %lu ms%s%s: %s",
                        static_cast<unsigned>(rule), kindName(alarm.kind), formatMilli(a, alarm.threshold), unit,
                        alarm.kind == AlarmKind::riseRate ? "/min" : "", severityName(alarm.severity),
                        formatMilli(b, alarm.hysteresis), static_cast<unsigned long>(alarm.delayMs),
                        alarm.latching ? ", latching" : "", channel.thresholdOverridden(rule) ? ", changed" : "", state);
        }
    }
}

void MeasurementConsole::reportSaved(std::size_t index, bool saved) const {
    if (saved) {
        log_.writef(LogLevel::info, "%s: stored", registry_.at(index).name());
    } else if (!registry_.settingsPersisted(index)) {
        log_.writef(LogLevel::warning, "%s: applied, but not stored (no sensor identity or no storage)",
                    registry_.at(index).name());
    }
}

void MeasurementConsole::calibrate(std::size_t index, std::int32_t reference, bool secondPoint) {
    auto& channel = registry_.at(index);
    if (!channel.valid()) {
        log_.writef(LogLevel::error, "%s: no valid reading to calibrate", channel.name());
        return;
    }
    char a[20];
    char b[20];
    const auto raw = channel.raw();
    Calibration calibration = channel.calibration();
    if (secondPoint) {
        const auto rawSpan = static_cast<std::int64_t>(raw) - pointRaw_;
        if (pointChannel_ != index || std::llabs(rawSpan) < 1000) {
            log_.writef(LogLevel::error, "%s: cal2 needs a first point (cal) at least 1 unit away", channel.name());
            return;
        }
        const auto gain = (static_cast<std::int64_t>(reference) - pointReference_) * 1000000 / rawSpan;
        if (gain < 500000 || gain > 2000000) {
            log_.writef(LogLevel::error, "%s: gain %ld ppm implausible, not applied", channel.name(),
                        static_cast<long>(gain));
            return;
        }
        calibration.gainPpm = static_cast<std::int32_t>(gain);
        calibration.offset = static_cast<std::int32_t>(pointReference_ - static_cast<std::int64_t>(pointRaw_) * gain / 1000000);
        pointChannel_ = ~std::size_t{};
    } else {
        calibration.offset = static_cast<std::int32_t>(reference - static_cast<std::int64_t>(raw) * calibration.gainPpm / 1000000);
        pointChannel_ = index;
        pointRaw_ = raw;
        pointReference_ = reference;
    }
    const bool saved = registry_.setCalibration(index, calibration);
    log_.writef(LogLevel::info, "%s: offset %s, gain %ld ppm, now %s %s", channel.name(),
                formatMilli(a, calibration.offset, 3u), static_cast<long>(calibration.gainPpm),
                formatMilli(b, channel.value(), 3u), unitName(channel.quantity()));
    reportSaved(index, saved);
}

void MeasurementConsole::sensorsCommand(void* context, std::size_t, const char* const*) {
    static_cast<MeasurementConsole*>(context)->list();
}

void MeasurementConsole::sensorCommand(void* context, std::size_t argc, const char* const* argv) {
    auto& self = *static_cast<MeasurementConsole*>(context);
    if (argc < 2u) {
        self.list();
        return;
    }
    const auto index = self.registry_.find(argv[1]);
    if (index >= self.registry_.size()) {
        self.log_.writef(LogLevel::error, "sensor: no channel '%s'", argv[1]);
        return;
    }
    if (argc == 2u) {
        self.detail(index);
        return;
    }
    const char* action = argv[2];
    std::int32_t number = 0;
    if ((std::strcmp(action, "cal") == 0 || std::strcmp(action, "cal2") == 0) && argc == 4u &&
        parseMilli(argv[3], number)) {
        self.calibrate(index, number, action[3] == '2');
    } else if (std::strcmp(action, "alarm") == 0 && argc == 5u && parseMilli(argv[4], number)) {
        char* end = nullptr;
        const auto rule = std::strtoul(argv[3], &end, 10);
        if (*end != '\0' || !self.registry_.at(index).setThreshold(rule, number)) {
            self.log_.writef(LogLevel::error, "sensor: no adjustable alarm rule %s", argv[3]);
            return;
        }
        self.reportSaved(index, self.registry_.setThreshold(index, rule, number));
        self.detail(index);
    } else if (std::strcmp(action, "reset") == 0 && argc == 3u) {
        const bool erased = self.registry_.resetSettings(index);
        self.log_.writef(LogLevel::info, "%s: defaults%s", self.registry_.at(index).name(),
                         erased ? ", stored settings erased" : "");
    } else if (std::strcmp(action, "stats") == 0 && argc == 3u) {
        self.registry_.at(index).resetStatistics();
        self.log_.writef(LogLevel::info, "%s: statistics restarted", self.registry_.at(index).name());
    } else {
        self.log_.write(LogLevel::error,
                        "usage: sensor <name> [cal|cal2 <ref> | alarm <rule> <value> | reset | stats]");
    }
}

void MeasurementConsole::ackCommand(void* context, std::size_t, const char* const*) {
    auto& self = *static_cast<MeasurementConsole*>(context);
    const auto before = self.registry_.summary();
    self.registry_.acknowledgeAll();
    if (!before.unacknowledged) self.log_.write(LogLevel::info, "ack: no unacknowledged alarm");
}

} // namespace slstm32::sensors
