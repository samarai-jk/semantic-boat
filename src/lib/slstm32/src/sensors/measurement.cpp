#include "slstm32/sensors/measurement.hpp"

namespace slstm32::sensors {

const char* quantityName(Quantity quantity) {
    switch (quantity) {
    case Quantity::temperature: return "temperature";
    case Quantity::relativeHumidity: return "humidity";
    case Quantity::voltage: return "voltage";
    case Quantity::illuminance: return "illuminance";
    case Quantity::uvIndex: return "UV index";
    case Quantity::pressure: return "pressure";
    case Quantity::angle: return "angle";
    case Quantity::concentration: return "concentration";
    case Quantity::rotationalSpeed: return "rotational speed";
    }
    return "?";
}

const char* unitName(Quantity quantity) {
    switch (quantity) {
    case Quantity::temperature: return "C";
    case Quantity::relativeHumidity: return "%";
    case Quantity::voltage: return "V";
    case Quantity::illuminance: return "lx";
    case Quantity::uvIndex: return "UVI";
    case Quantity::pressure: return "hPa";
    case Quantity::angle: return "deg";
    case Quantity::concentration: return "ppm";
    case Quantity::rotationalSpeed: return "rpm";
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

const char* comparisonName(Comparison comparison) {
    switch (comparison) {
    case Comparison::above: return ">";
    case Comparison::atLeast: return ">=";
    case Comparison::below: return "<";
    case Comparison::atMost: return "<=";
    case Comparison::inside: return "inside";
    case Comparison::outside: return "outside";
    }
    return "?";
}

const char* tendencyName(Tendency tendency) {
    switch (tendency) {
    case Tendency::unknown: return "unknown";
    case Tendency::steady: return "steady";
    case Tendency::rising: return "rising";
    case Tendency::falling: return "falling";
    case Tendency::risingRapidly: return "rising rapidly";
    case Tendency::fallingRapidly: return "falling rapidly";
    }
    return "?";
}

Measurement::Measurement(const MeasurementConfig& config)
    : config_(config), faultRule_(config.fault), output_(config.output), trendWindowMs_(config.trendWindowMs) {
    // Windows come from the registry's pool; MeasurementRegistry::init()
    // applies these defaults (or the stored rules) with their windows.
    const auto count = config.alarms ? (config.alarmCount < maxAlarms ? config.alarmCount : maxAlarms) : 0u;
    for (std::size_t index = 0u; index < count; ++index) rules_[index] = config.alarms[index];
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
    samples_ = 0u;
    rateCount_ = 0u;
    rateValid_ = false;
    resetTrend();
}

void Measurement::update(std::int32_t raw, std::uint32_t now) {
    raw_ = raw;
    value_ = static_cast<std::int32_t>(static_cast<std::int64_t>(raw) * calibration_.gainPpm / 1000000 +
                                       calibration_.offset);
    rawValue_ = value_;
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
    for (auto* window : windows_) {
        if (window) window->add(value_, now);
    }
    if (outputWindow_) outputWindow_->add(value_, now);
    trackRate(now);
    trackTrend(now);
    evaluate(now);
}

bool Measurement::output(std::int32_t& value) const {
    if (status_ != Status::valid) return false;
    if (outputWindow_) return outputWindow_->result(value);
    value = value_;
    return true;
}

bool Measurement::functionValue(ValueFunction function, const Window* window, std::int32_t& value) const {
    switch (function) {
    case ValueFunction::none: return false;
    case ValueFunction::last:
        value = value_;
        return status_ == Status::valid;
    case ValueFunction::rate:
        value = ratePerMinute_;
        return rateValid_;
    case ValueFunction::average:
    case ValueFunction::minimum:
    case ValueFunction::maximum:
    case ValueFunction::change:
        return window && window->result(value);
    }
    return false;
}

void Measurement::setTrendBuffer(TrendPoint* points, std::size_t count) {
    trendPoints_ = count >= 2u ? points : nullptr;
    trendCapacity_ = trendPoints_ ? count : 0u;
    resetTrend();
}

void Measurement::setTrendWindow(std::uint32_t windowMs) {
    trendWindowMs_ = windowMs;
    resetTrend();
}

void Measurement::resetTrend() {
    trendHead_ = 0u;
    trendCount_ = 0u;
    trendChange_ = 0;
    trendSpanMs_ = 0u;
    trendValid_ = false;
}

void Measurement::trackTrend(std::uint32_t now) {
    if (!trendPoints_ || trendWindowMs_ == 0u) return;
    const std::uint32_t interval = trendWindowMs_ / static_cast<std::uint32_t>(trendCapacity_ - 1u);
    const auto newest = (trendHead_ + trendCapacity_ - 1u) % trendCapacity_;
    if (trendCount_ == 0u || static_cast<std::uint32_t>(now - trendPoints_[newest].at) >= interval) {
        trendPoints_[trendHead_] = {now, filtered_};
        trendHead_ = (trendHead_ + 1u) % trendCapacity_;
        if (trendCount_ < trendCapacity_) ++trendCount_;
    }
    // Forget samples from before a gap (sensor missing for longer than the window).
    while (trendCount_ > 1u) {
        const auto oldest = (trendHead_ + trendCapacity_ - trendCount_) % trendCapacity_;
        if (static_cast<std::uint32_t>(now - trendPoints_[oldest].at) <= trendWindowMs_ + interval) break;
        --trendCount_;
    }
    const auto oldest = (trendHead_ + trendCapacity_ - trendCount_) % trendCapacity_;
    trendSpanMs_ = static_cast<std::uint32_t>(now - trendPoints_[oldest].at);
    trendChange_ = filtered_ - trendPoints_[oldest].value;
    trendValid_ = trendCount_ >= 2u;
}

Tendency Measurement::tendency() const {
    if (!trendValid_) return Tendency::unknown;
    const auto magnitude = trendChange_ < 0 ? -trendChange_ : trendChange_;
    if (magnitude < config_.trendSteady || magnitude == 0) return Tendency::steady;
    const bool rapid = config_.trendRapid != 0 && magnitude > config_.trendRapid;
    if (trendChange_ > 0) return rapid ? Tendency::risingRapidly : Tendency::rising;
    return rapid ? Tendency::fallingRapidly : Tendency::falling;
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
    for (auto* window : windows_) {
        if (window) window->advance(now);
    }
    if (outputWindow_) outputWindow_->advance(now);
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

bool Measurement::alarmUsed(std::size_t slot) const {
    if (slot < maxAlarms) return rules_[slot].function != ValueFunction::none;
    return slot == faultSlot && faultRule_.enabled;
}

Severity Measurement::alarmSeverity(std::size_t slot) const {
    return slot < maxAlarms ? rules_[slot].severity : faultRule_.severity;
}

bool Measurement::condition(std::size_t slot, bool active, bool& known) const {
    known = true;
    if (slot == faultSlot) return status_ == Status::fault || status_ == Status::stale;
    const auto& rule = rules_[slot];
    std::int32_t value = 0;
    // Value rules keep their state while there is no valid value; the fault
    // alarm covers that case.
    if (status_ != Status::valid || !functionValue(rule.function, windows_[slot], value)) {
        known = false;
        return false;
    }
    const auto v = static_cast<std::int64_t>(value);
    const auto low = static_cast<std::int64_t>(rule.threshold);
    const auto high = static_cast<std::int64_t>(rule.upper);
    const auto h = active ? static_cast<std::int64_t>(rule.hysteresis) : 0;
    switch (rule.comparison) {
    case Comparison::above: return v > low - h;
    case Comparison::atLeast: return v >= low - h;
    case Comparison::below: return v < low + h;
    case Comparison::atMost: return v <= low + h;
    case Comparison::inside: return v >= low - h && v <= high + h;
    case Comparison::outside: return v < low + h || v > high - h;
    }
    return false;
}

void Measurement::evaluate(std::uint32_t now) {
    for (std::size_t slot = 0u; slot < alarmSlots; ++slot) {
        if (!alarmUsed(slot)) continue;
        bool known = true;
        const bool holds = condition(slot, alarms_[slot].active, known);
        if (!known) continue;
        const bool fault = slot == faultSlot;
        const auto delaySeconds = fault ? faultRule_.delaySeconds : rules_[slot].delaySeconds;
        evaluateSlot(slot, holds, static_cast<std::uint32_t>(delaySeconds) * 1000u,
                     fault ? faultRule_.latching : rules_[slot].latching, now);
    }
}

void Measurement::evaluateSlot(std::size_t slot, bool holds, std::uint32_t delayMs, bool latching, std::uint32_t now) {
    auto& state = alarms_[slot];
    if (!state.active) {
        if (!holds) {
            state.pending = false;
            return;
        }
        if (!state.pending) {
            state.pending = true;
            state.pendingSince = now;
        }
        if (static_cast<std::uint32_t>(now - state.pendingSince) >= delayMs) {
            state = {false, true, false, false, now};
            notify(slot, AlarmTransition::activated);
        }
    } else if (holds) {
        state.conditionCleared = false;
    } else if (latching && !state.acknowledged) {
        state.conditionCleared = true;
    } else {
        state = {};
        notify(slot, AlarmTransition::cleared);
    }
}

void Measurement::acknowledge() {
    for (std::size_t slot = 0u; slot < alarmSlots; ++slot) {
        auto& state = alarms_[slot];
        if (!state.active) continue;
        if (!state.acknowledged) {
            state.acknowledged = true;
            notify(slot, AlarmTransition::acknowledged);
        }
        if (state.conditionCleared) {
            state = {};
            notify(slot, AlarmTransition::cleared);
        }
    }
}

void Measurement::clearSlot(std::size_t slot) {
    const bool wasActive = alarms_[slot].active;
    alarms_[slot] = {};
    if (wasActive) notify(slot, AlarmTransition::cleared);
}

bool Measurement::validRule(const AlarmRule& rule) {
    if (rule.function > ValueFunction::rate || rule.comparison > Comparison::outside ||
        rule.severity > Severity::emergency || rule.hysteresis < 0) {
        return false;
    }
    if (windowed(rule.function) && rule.window.length == 0u) return false;
    const bool band = rule.comparison == Comparison::inside || rule.comparison == Comparison::outside;
    return !band || rule.upper >= rule.threshold;
}

bool Measurement::validRule(const OutputRule& rule) {
    if (rule.function != ValueFunction::last && rule.function != ValueFunction::average &&
        rule.function != ValueFunction::minimum && rule.function != ValueFunction::maximum) {
        return false;
    }
    return !windowed(rule.function) || rule.window.length != 0u;
}

Measurement::RuleResult Measurement::setWindow(Window*& window, ValueFunction function, WindowLength length,
                                               WindowPool* pool, std::uint32_t now) {
    if (!windowed(function)) {
        WindowPool::release(window);
        window = nullptr;
        return RuleResult::applied;
    }
    auto* target = window ? window : pool ? pool->acquire() : nullptr;
    if (!target) return RuleResult::noWindow;
    if (!target->start(function, length, now)) {
        if (!window) WindowPool::release(target);
        return RuleResult::invalid;
    }
    window = target;
    return RuleResult::applied;
}

Measurement::RuleResult Measurement::setAlarmRule(std::size_t slot, const AlarmRule& rule, WindowPool* pool,
                                                  std::uint32_t now) {
    if (slot >= maxAlarms || !validRule(rule)) return RuleResult::invalid;
    const auto& old = rules_[slot];
    // A changed threshold keeps the window's history; a changed window restarts it.
    const bool sameWindow = windows_[slot] && old.function == rule.function &&
                            old.window.length == rule.window.length && old.window.samples == rule.window.samples;
    auto* window = windows_[slot];
    if (!sameWindow && windowed(rule.function) && !window) {
        window = pool ? pool->acquire() : nullptr;
        if (!window) return RuleResult::noWindow;
    }
    clearSlot(slot); // still with the old rule and window, for its log line
    if (!sameWindow && !windowed(rule.function)) {
        WindowPool::release(window);
        window = nullptr;
    } else if (!sameWindow) {
        window->start(rule.function, rule.window, now);
    }
    windows_[slot] = window;
    rules_[slot] = rule;
    return RuleResult::applied;
}

Measurement::RuleResult Measurement::setFaultRule(const FaultRule& rule) {
    if (rule.severity > Severity::emergency) return RuleResult::invalid;
    clearSlot(faultSlot);
    faultRule_ = rule;
    return RuleResult::applied;
}

Measurement::RuleResult Measurement::setOutputRule(const OutputRule& rule, WindowPool* pool, std::uint32_t now) {
    if (!validRule(rule)) return RuleResult::invalid;
    const bool sameWindow = outputWindow_ && output_.function == rule.function &&
                            output_.window.length == rule.window.length && output_.window.samples == rule.window.samples;
    if (!sameWindow) {
        const auto result = setWindow(outputWindow_, rule.function, rule.window, pool, now);
        if (result != RuleResult::applied) return result;
    }
    output_ = rule;
    return RuleResult::applied;
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
        rawValue_ = filtered_ = value_;
        rateCount_ = 0u;
        rateValid_ = false;
        resetTrend();
        // The windows hold values of the old calibration: start them again.
        for (std::size_t slot = 0u; slot < maxAlarms; ++slot) {
            if (windows_[slot]) windows_[slot]->start(rules_[slot].function, rules_[slot].window, lastUpdateAt_);
        }
        if (outputWindow_) outputWindow_->start(output_.function, output_.window, lastUpdateAt_);
    }
}

void Measurement::notify(std::size_t slot, AlarmTransition transition) {
    if (listener_) listener_(listenerContext_, *this, slot, transition);
}

} // namespace slstm32::sensors
