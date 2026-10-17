#include "slstm32/sensors/measurement.hpp"

namespace slstm32::sensors {

const char* quantityName(Quantity quantity) {
    switch (quantity) {
    case Quantity::temperature: return "temperature";
    case Quantity::relativeHumidity: return "humidity";
    case Quantity::voltage: return "voltage";
    }
    return "?";
}

const char* unitName(Quantity quantity) {
    switch (quantity) {
    case Quantity::temperature: return "C";
    case Quantity::relativeHumidity: return "%";
    case Quantity::voltage: return "V";
    }
    return "";
}

const char* statusName(Status status) {
    switch (status) {
    case Status::unknown: return "unknown";
    case Status::valid: return "valid";
    case Status::stale: return "stale";
    case Status::fault: return "fault";
    }
    return "?";
}

const char* faultName(Fault fault) {
    switch (fault) {
    case Fault::none: return "none";
    case Fault::noResponse: return "no response";
    case Fault::checksum: return "checksum";
    case Fault::outOfRange: return "out of range";
    case Fault::sensorReported: return "sensor reported";
    case Fault::notReady: return "not ready";
    }
    return "?";
}

const char* severityName(Severity severity) {
    switch (severity) {
    case Severity::warning: return "WARNING";
    case Severity::alarm: return "ALARM";
    case Severity::emergency: return "EMERGENCY";
    }
    return "?";
}

Measurement::Measurement(const MeasurementConfig& config) : config_(config) {
    alarmCount_ = config.alarms ? (config.alarmCount < maxAlarms ? config.alarmCount : maxAlarms) : 0u;
    for (std::size_t index = 0u; index < alarmCount_; ++index) rules_[index] = config.alarms[index];
}

void Measurement::setIdentity(const Identity& identity) {
    if (identity.kind == identity_.kind && identity.id == identity_.id && identity.subchannel == identity_.subchannel) {
        return;
    }
    const bool replaced = identity_.known();
    identity_ = identity;
    ++identityRevision_;
    if (!replaced) return;
    calibration_ = {};
    for (std::size_t index = 0u; index < alarmCount_; ++index) rules_[index].threshold = config_.alarms[index].threshold;
    samples_ = 0u;
    rateCount_ = 0u;
    rateValid_ = false;
}

void Measurement::update(std::int32_t raw, std::uint32_t now) {
    raw_ = raw;
    value_ = static_cast<std::int32_t>(static_cast<std::int64_t>(raw) * calibration_.gainPpm / 1000000 +
                                       calibration_.offset);
    if (config_.filterMs == 0u || status_ != Status::valid) {
        filtered_ = value_;
    } else {
        const auto elapsed = static_cast<std::int64_t>(static_cast<std::uint32_t>(now - lastUpdateAt_));
        filtered_ += static_cast<std::int32_t>((static_cast<std::int64_t>(value_) - filtered_) * elapsed /
                                               (static_cast<std::int64_t>(config_.filterMs) + elapsed));
    }
    if (samples_ == 0u || value_ < minimum_) minimum_ = value_;
    if (samples_ == 0u || value_ > maximum_) maximum_ = value_;
    ++samples_;
    status_ = Status::valid;
    fault_ = Fault::none;
    lastUpdateAt_ = now;
    trackRate(now);
    evaluate(now);
}

void Measurement::fail(Fault fault, std::uint32_t now) {
    status_ = Status::fault;
    fault_ = fault;
    rateCount_ = 0u;
    rateValid_ = false;
    evaluate(now);
}

void Measurement::tick(std::uint32_t now) {
    if (!ticked_) {
        ticked_ = true;
        firstTickAt_ = now;
    }
    const auto staleAfter = config_.staleAfterMs;
    if (status_ == Status::valid && static_cast<std::uint32_t>(now - lastUpdateAt_) > staleAfter) {
        status_ = Status::stale;
        rateCount_ = 0u;
        rateValid_ = false;
    } else if (status_ == Status::unknown && static_cast<std::uint32_t>(now - firstTickAt_) > staleAfter) {
        status_ = Status::stale;
    }
    evaluate(now);
}

void Measurement::trackRate(std::uint32_t now) {
    const auto newest = rateCount_ == 0u ? 0u : (rateHead_ + ratePoints - 1u) % ratePoints;
    if (rateCount_ == 0u || static_cast<std::uint32_t>(now - rate_[newest].at) >= rateStepMs) {
        rate_[rateHead_] = {now, filtered_};
        rateHead_ = (rateHead_ + 1u) % ratePoints;
        if (rateCount_ < ratePoints) ++rateCount_;
    }
    const auto oldest = rateCount_ < ratePoints ? 0u : rateHead_;
    const auto span = static_cast<std::uint32_t>(now - rate_[oldest].at);
    rateValid_ = rateCount_ > 1u && span >= rateMinimumSpanMs;
    if (rateValid_) {
        ratePerMinute_ = static_cast<std::int32_t>((static_cast<std::int64_t>(filtered_) - rate_[oldest].value) *
                                                   60000 / static_cast<std::int64_t>(span));
    }
}

bool Measurement::condition(const AlarmRule& rule, bool active) const {
    const auto value = rule.useFiltered ? filtered_ : value_;
    switch (rule.kind) {
    case AlarmKind::high:
        return active ? value > rule.threshold - rule.hysteresis : value > rule.threshold;
    case AlarmKind::low:
        return active ? value < rule.threshold + rule.hysteresis : value < rule.threshold;
    case AlarmKind::riseRate:
        return rateValid_ &&
               (active ? ratePerMinute_ > rule.threshold - rule.hysteresis : ratePerMinute_ > rule.threshold);
    case AlarmKind::sensorFault:
        return status_ == Status::fault || status_ == Status::stale;
    }
    return false;
}

void Measurement::evaluate(std::uint32_t now) {
    for (std::size_t index = 0u; index < alarmCount_; ++index) {
        const auto& rule = rules_[index];
        auto& state = alarms_[index];
        // Value rules keep their state while there is no valid value; the
        // sensorFault rule covers that case.
        if (rule.kind != AlarmKind::sensorFault && status_ != Status::valid) continue;
        const bool holds = condition(rule, state.active);
        if (!state.active) {
            if (!holds) {
                state.pending = false;
                continue;
            }
            if (!state.pending) {
                state.pending = true;
                state.pendingSince = now;
            }
            if (static_cast<std::uint32_t>(now - state.pendingSince) >= rule.delayMs) {
                state = {false, true, false, false, now};
                notify(index, AlarmTransition::activated);
            }
        } else if (holds) {
            state.conditionCleared = false;
        } else if (rule.latching && !state.acknowledged) {
            state.conditionCleared = true;
        } else {
            state = {};
            notify(index, AlarmTransition::cleared);
        }
    }
}

void Measurement::acknowledge() {
    for (std::size_t index = 0u; index < alarmCount_; ++index) {
        auto& state = alarms_[index];
        if (!state.active) continue;
        if (!state.acknowledged) {
            state.acknowledged = true;
            notify(index, AlarmTransition::acknowledged);
        }
        if (state.conditionCleared) {
            state = {};
            notify(index, AlarmTransition::cleared);
        }
    }
}

void Measurement::resetStatistics() {
    samples_ = 0u;
    minimum_ = maximum_ = value_;
}

void Measurement::setCalibration(const Calibration& calibration) {
    calibration_ = calibration;
    if (samples_ != 0u) {
        value_ = static_cast<std::int32_t>(static_cast<std::int64_t>(raw_) * calibration_.gainPpm / 1000000 +
                                           calibration_.offset);
        filtered_ = value_;
        rateCount_ = 0u;
        rateValid_ = false;
    }
}

bool Measurement::setThreshold(std::size_t rule, std::int32_t threshold) {
    if (rule >= alarmCount_ || rules_[rule].kind == AlarmKind::sensorFault) return false;
    rules_[rule].threshold = threshold;
    return true;
}

void Measurement::notify(std::size_t rule, AlarmTransition transition) {
    if (listener_) listener_(listenerContext_, *this, rule, transition);
}

} // namespace slstm32::sensors
