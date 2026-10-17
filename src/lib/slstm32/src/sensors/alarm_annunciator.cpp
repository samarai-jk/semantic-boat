#include "slstm32/sensors/alarm_annunciator.hpp"

namespace slstm32::sensors {

bool AlarmAnnunciator::same(const AlarmSummary& a, const AlarmSummary& b) {
    return a.active == b.active && a.unacknowledged == b.unacknowledged && a.highest == b.highest &&
           a.highestUnacknowledged == b.highestUnacknowledged;
}

bool AlarmAnnunciator::init() {
    if (config_.indicator) config_.indicator(config_.indicatorContext, last_);
    return true;
}

void AlarmAnnunciator::silence() {
    silenced_ = true;
    if (sounding_) player_.stop();
    sounding_ = nullptr;
}

void AlarmAnnunciator::run() {
    const auto summary = registry_.summary();
    if (!same(summary, last_)) {
        // A new or more severe unacknowledged alarm ends a silence.
        if (summary.unacknowledged &&
            (!last_.unacknowledged || summary.highestUnacknowledged > last_.highestUnacknowledged)) {
            silenced_ = false;
        }
        last_ = summary;
        if (config_.indicator) config_.indicator(config_.indicatorContext, summary);
    }
    const drivers::TonePattern* wanted = nullptr;
    if (summary.unacknowledged && !silenced_) {
        wanted = config_.patterns[static_cast<unsigned>(summary.highestUnacknowledged)];
    }
    if (wanted == sounding_) return;
    // Stop only our own pattern; other users of the tone (feedback clicks)
    // are left alone while no alarm sounds.
    if (sounding_ && player_.pattern() == sounding_) player_.stop();
    sounding_ = wanted;
    if (wanted) player_.play(*wanted);
}

} // namespace slstm32::sensors
