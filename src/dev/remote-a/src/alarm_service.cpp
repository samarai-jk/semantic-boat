#include "alarm_service.hpp"
#include <cstring>

namespace remote_a {
namespace {

constexpr auto noRecord = AlarmService::capacity;

} // namespace

bool AlarmService::init() {
    link_.setAlertReceiver({this, &AlarmService::updateThunk,
                            &AlarmService::removeThunk, &AlarmService::silenceThunk});
    return true;
}

void AlarmService::copy(std::string_view source, char* target, std::size_t capacity) {
    if (!target || capacity == 0u) return;
    const auto size = source.size() < capacity - 1u ? source.size() : capacity - 1u;
    if (size != 0u) std::memcpy(target, source.data(), size);
    target[size] = '\0';
}

bool AlarmService::equals(const char* stored, std::string_view value) {
    return stored && std::strlen(stored) == value.size() &&
        std::memcmp(stored, value.data(), value.size()) == 0;
}

void AlarmService::updateThunk(void* context, std::uint8_t level,
                               std::uint32_t occurrence, std::string_view id,
                               std::string_view title, std::string_view message) {
    static_cast<AlarmService*>(context)->update(level, occurrence, id, title, message);
}

void AlarmService::removeThunk(void* context, std::string_view id) {
    static_cast<AlarmService*>(context)->remove(id);
}

void AlarmService::silenceThunk(void* context, std::string_view id) {
    static_cast<AlarmService*>(context)->silence(id);
}

void AlarmService::actionThunk(void* context, semantic_display::AlertAction action) {
    static_cast<AlarmService*>(context)->action(action);
}

void AlarmService::presentedThunk(void* context) {
    static_cast<AlarmService*>(context)->presented();
}

std::size_t AlarmService::find(std::string_view id) const {
    for (std::size_t index = 0u; index < records_.size(); ++index) {
        if (records_[index].active && equals(records_[index].id.data(), id)) return index;
    }
    return noRecord;
}

std::size_t AlarmService::allocate(semantic_display::AlertLevel incoming) {
    for (std::size_t index = 0u; index < records_.size(); ++index) {
        if (!records_[index].active) return index;
    }

    // Hidden and locally filtered records are the safest entries to replace.
    std::size_t candidate = noRecord;
    for (std::size_t index = 0u; index < records_.size(); ++index) {
        const auto& record = records_[index];
        if (!record.hidden && record.level >= minimumLevel_) continue;
        if (candidate == noRecord || record.order < records_[candidate].order) candidate = index;
    }
    if (candidate != noRecord) return candidate;

    // Otherwise retain older alerts at the same priority. A genuinely more
    // important arrival may replace the oldest member of the lowest priority.
    auto lowest = records_[0].level;
    for (const auto& record : records_) if (record.level < lowest) lowest = record.level;
    if (incoming <= lowest) return noRecord;
    for (std::size_t index = 0u; index < records_.size(); ++index) {
        if (records_[index].level != lowest) continue;
        if (candidate == noRecord || records_[index].order < records_[candidate].order) {
            candidate = index;
        }
    }
    return candidate;
}

void AlarmService::update(std::uint8_t rawLevel, std::uint32_t occurrence,
                          std::string_view id, std::string_view title,
                          std::string_view message) {
    if (rawLevel > static_cast<std::uint8_t>(semantic_display::AlertLevel::emergency) ||
        id.empty()) return;
    const auto level = static_cast<semantic_display::AlertLevel>(rawLevel);
    auto index = find(id);
    if (index == noRecord) {
        index = allocate(level);
        if (index == noRecord) {
            if (overflowCount_ != UINT16_MAX) ++overflowCount_;
            display_.updateAlertQueue(visibleCount(), overflowCount_);
            return;
        }
        records_[index] = {};
        records_[index].active = true;
        records_[index].order = nextOrder_++;
        if (nextOrder_ == 0u) nextOrder_ = 1u;
    } else {
        auto& existing = records_[index];
        if (occurrence == existing.occurrence && level == existing.level &&
            equals(existing.title.data(), title) &&
            equals(existing.message.data(), message)) {
            // A server snapshot that still contains an alert after our global
            // action is authoritative: the action did not take effect, so
            // present it again. Explicit local Hide remains sticky.
            if (!existing.globalActionPending || existing.locallyHidden) return;
            existing.globalActionPending = false;
            existing.hidden = false;
            showCurrent();
            return;
        }
        const bool repeated = occurrence != existing.occurrence;
        const bool escalated = level > existing.level;
        if (repeated || escalated) {
            existing.hidden = false;
            existing.locallyHidden = false;
            existing.globalActionPending = false;
            existing.silenced = false;
            existing.order = nextOrder_++;
            if (nextOrder_ == 0u) nextOrder_ = 1u;
        }
    }

    auto& record = records_[index];
    record.level = level;
    record.occurrence = occurrence;
    copy(id, record.id.data(), record.id.size());
    copy(title, record.title.data(), record.title.size());
    copy(message, record.message.data(), record.message.size());
    if (current_ != noRecord && current_ != index && selectCurrent() == current_) {
        display_.updateAlertQueue(visibleCount(), overflowCount_);
        return;
    }
    showCurrent();
}

void AlarmService::remove(std::string_view id) {
    const auto index = find(id);
    if (index == noRecord) return;
    const bool wasCurrent = index == current_;
    records_[index] = {};
    if (wasCurrent) {
        showCurrent();
    } else {
        display_.updateAlertQueue(visibleCount(), overflowCount_);
    }
}

void AlarmService::silence(std::string_view id) {
    const auto index = find(id);
    if (index == noRecord) return;
    records_[index].silenced = true;
    if (index == current_) feedback_.setSystemAlertAudible(false);
}

std::uint8_t AlarmService::visibleCount() const {
    std::uint8_t count{};
    for (const auto& record : records_) {
        if (record.active && !record.hidden && record.level >= minimumLevel_) ++count;
    }
    return count;
}

std::size_t AlarmService::selectCurrent() const {
    std::size_t selected = noRecord;
    for (std::size_t index = 0u; index < records_.size(); ++index) {
        const auto& record = records_[index];
        if (!record.active || record.hidden || record.level < minimumLevel_) continue;
        if (selected == noRecord || record.level > records_[selected].level ||
            (record.level == records_[selected].level &&
             record.order < records_[selected].order)) selected = index;
    }
    return selected;
}

void AlarmService::showCurrent(bool resetFeedback) {
    current_ = selectCurrent();
    if (current_ == noRecord) {
        bool anyActive{};
        for (const auto& record : records_) anyActive = anyActive || record.active;
        if (!anyActive) overflowCount_ = 0u;
        display_.dismissAlert();
        feedback_.setSystemAlert(semantic_display::AlertLevel::info, false);
        return;
    }
    const auto& record = records_[current_];
    if (resetFeedback) feedback_.setSystemAlert(record.level, true, false);
    display_.showAlert(record.title.data(), record.message.data(), record.level,
                       1u, visibleCount(), overflowCount_,
                       {this, &AlarmService::actionThunk, &AlarmService::presentedThunk});
}

void AlarmService::action(semantic_display::AlertAction action) {
    if (current_ == noRecord || !records_[current_].active) return;
    auto& record = records_[current_];
    if (action == semantic_display::AlertAction::silence) {
        record.silenced = true;
        feedback_.setSystemAlertAudible(false);
        (void)link_.sendAlertAction(semantic_link::AlertAction::silence,
                                    record.occurrence, record.id.data());
        return;
    }
    bool sent{true};
    if (action == semantic_display::AlertAction::acknowledge) {
        sent = link_.sendAlertAction(semantic_link::AlertAction::acknowledge,
                                     record.occurrence, record.id.data());
    } else if (action == semantic_display::AlertAction::snooze) {
        sent = link_.sendAlertAction(semantic_link::AlertAction::snooze,
                                     record.occurrence, record.id.data());
    }
    if (!sent) return;
    // Acknowledge, snooze, and local Hide all leave the record available for
    // deduplication while removing it from this device's presentation queue.
    record.hidden = true;
    record.locallyHidden = action == semantic_display::AlertAction::hideHere;
    record.globalActionPending = !record.locallyHidden;
    feedback_.setSystemAlertAudible(false);
    showCurrent();
}

void AlarmService::presented() {
    if (current_ == noRecord || !records_[current_].active) return;
    const auto& record = records_[current_];
    if (!record.silenced && record.level == semantic_display::AlertLevel::warning) {
        feedback_.playWarningCue();
    } else {
        feedback_.setSystemAlertAudible(!record.silenced);
    }
}

void AlarmService::setMinimumLevel(semantic_display::AlertLevel level) {
    if (level > semantic_display::AlertLevel::emergency) {
        level = semantic_display::AlertLevel::emergency;
    }
    minimumLevel_ = level;
    showCurrent();
}

} // namespace remote_a
