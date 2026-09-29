#include "feedback_service.hpp"
#include "events.hpp"
#include "slstm32/drivers/button.hpp"

namespace remote_a {
namespace {

constexpr std::uint32_t buttonBeepFrequencyHz = 2500u;
constexpr std::uint32_t buttonBeepDurationMs = 40u;
constexpr std::uint32_t wakeBeepFrequencyHz = 2200u;
constexpr std::uint32_t wakeBeepDurationMs = 50u;
constexpr std::uint32_t wakeBeepGapMs = 100u;
constexpr std::uint8_t wakeBeepCount = 3u;
constexpr std::uint32_t sleepBeepFrequencyHz = 2000u;
constexpr std::uint32_t sleepBeepDurationMs = 350u;
constexpr std::uint32_t welcomeBeepFrequencyHz = 1800u;
constexpr std::uint32_t welcomeBeepDurationMs = 90u;
constexpr std::uint32_t attentionBeepFrequencyHz = 2800u;
constexpr std::uint32_t attentionBeepDurationMs = 70u;
constexpr std::uint32_t attentionBeepGapMs = 180u;
constexpr std::uint8_t attentionBeepCount = 3u;
constexpr std::uint32_t warningBeepFrequencyHz = 1800u;
constexpr std::uint32_t warningBeepDurationMs = 120u;
// A regular fixed-pitch pulse is urgent without sounding like the much faster,
// alternating emergency tone. Keeping the alarm cadence simple also makes the
// two meanings easier to learn and recognize in a noisy cockpit.
constexpr std::uint32_t alarmToneFrequencyHz = 1800u;
constexpr std::uint32_t alarmToneDurationMs = 250u;
constexpr std::uint32_t alarmTonePeriodMs = 500u;
constexpr std::uint32_t emergencyToneStepMs = 125u;
constexpr std::uint32_t emergencyToneHoldMs = 175u;
constexpr std::uint32_t signalLedPulseMs = 1u;
constexpr float normalStatusLedDuty = 0.01f;
constexpr float subduedStatusLedDuty = 0.001f;
// Board PWM maps normalized tone level 1.0 to 50% timer duty.
constexpr float minimumBeepLevel = 0.04f; // 2% timer duty
constexpr float mediumBeepLevel = 0.10f;  // 5% timer duty

} // namespace

bool FeedbackService::init() {
    if (!runtime_.millis || !events_.subscribe(buttonPressed, &FeedbackService::eventThunk, this)) {
        return false;
    }
    starting_ = true;
    initialized_ = true;
    updateStatusLed();
    return true;
}

void FeedbackService::eventThunk(slstm32::EventId, const void* payload,
                                 std::uint8_t size, void* context) {
    if (!payload || size != sizeof(slstm32::drivers::ButtonEvent) || !context) return;
    const auto& event = *static_cast<const slstm32::drivers::ButtonEvent*>(payload);
    if (event.pressed) static_cast<FeedbackService*>(context)->onButton();
}

void FeedbackService::onButton() {
    // Waking the E-paper panel performs a blocking initialization. Starting a
    // tone here would keep it sounding until that initialization returns.
    // setSleeping(false) starts the cue after the panel is awake instead.
    // Further edges from the wake press must not replace the wake sequence
    // with the ordinary button tone.
    if (sleeping_ || wakeCueActive_ || attentionCueActive_ || systemAlertActive_) return;

    if (settings_.ledMode == LedMode::normal) signalLed_.pulse(signalLedPulseMs);
    (void)buzzer_.play(buttonBeepFrequencyHz, buttonBeepDurationMs, beepLevel());
}

void FeedbackService::setAlert(bool active) {
    alert_ = active;
    updateStatusLed();
}

void FeedbackService::playAttentionCue() {
    startAttentionCue(1.0f);
}

void FeedbackService::playWarningCue() {
    if (!systemAlertActive_ || systemAlertLevel_ != semantic_display::AlertLevel::warning) return;
    buzzer_.stop();
    (void)buzzer_.play(warningBeepFrequencyHz, warningBeepDurationMs, 1.0f);
}

void FeedbackService::setSystemAlert(semantic_display::AlertLevel level, bool active,
                                     bool audible) {
    systemAlertLevel_ = level;
    systemAlertActive_ = active;
    systemAlertAudible_ = active && audible &&
        level >= semantic_display::AlertLevel::alarm;
    attentionCueActive_ = false;
    if (!systemAlertAudible_) buzzer_.stop();
    emergencyHighTone_ = true;
    nextSystemToneAt_ = runtime_.millis();
    updateStatusLed();
}

void FeedbackService::setSystemAlertAudible(bool audible) {
    systemAlertAudible_ = systemAlertActive_ && audible &&
        systemAlertLevel_ >= semantic_display::AlertLevel::alarm;
    if (!systemAlertAudible_) buzzer_.stop();
    nextSystemToneAt_ = runtime_.millis();
}

void FeedbackService::setOperational() {
    if (operational_) return;
    operational_ = true;
    starting_ = false;
    updateStatusLed();
    if (!alert_) {
        (void)buzzer_.play(welcomeBeepFrequencyHz, welcomeBeepDurationMs, beepLevel());
    }
}

void FeedbackService::startAttentionCue(float level) {
    wakeCueActive_ = false;
    if (level <= 0.0f) {
        attentionCueActive_ = false;
        attentionCueBeeps_ = 0u;
        return;
    }
    buzzer_.stop();
    attentionCueLevel_ = level;
    attentionCueActive_ = true;
    attentionCueBeeps_ = 0u;
    nextAttentionCueAt_ = runtime_.millis();
}

void FeedbackService::setSleeping(bool active) {
    sleeping_ = active;
    starting_ = false;
    if (active) {
        statusLed_.setColor({});
        wakeCueActive_ = false;
        attentionCueActive_ = false;
        sleepCueActive_ = buzzer_.play(
            sleepBeepFrequencyHz, sleepBeepDurationMs, beepLevel());
        return;
    }

    sleepCueActive_ = false;
    if (beepLevel() > 0.0f) {
        wakeCueActive_ = true;
        wakeCueBeeps_ = 1u;
        nextWakeCueAt_ = runtime_.millis() + wakeBeepDurationMs + wakeBeepGapMs;
        (void)buzzer_.play(wakeBeepFrequencyHz, wakeBeepDurationMs, beepLevel());
    } else {
        wakeCueActive_ = false;
        wakeCueBeeps_ = 0u;
    }
    updateStatusLed();
}

void FeedbackService::shutdown() {
    sleeping_ = true;
    starting_ = false;
    alert_ = false;
    sleepCueActive_ = false;
    wakeCueActive_ = false;
    attentionCueActive_ = false;
    systemAlertActive_ = false;
    systemAlertAudible_ = false;
    buzzer_.stop();
    signalLed_.off();
    statusLed_.off();
}

void FeedbackService::setSettings(DeviceSettings settings) {
    settings_ = settings;
    if (!initialized_) return;
    if (settings_.ledMode != LedMode::normal) signalLed_.off();
    updateStatusLed();
}

float FeedbackService::beepLevel() const {
    switch (settings_.beepVolume) {
    case BeepVolume::off: return 0.0f;
    case BeepVolume::minimum: return minimumBeepLevel;
    case BeepVolume::medium: return mediumBeepLevel;
    case BeepVolume::maximum: return 1.0f;
    default: return minimumBeepLevel;
    }
}

float FeedbackService::statusLedDuty() const {
    if (settings_.ledMode == LedMode::off) return 0.0f;
    return settings_.ledMode == LedMode::subdued ? subduedStatusLedDuty
                                                 : normalStatusLedDuty;
}

void FeedbackService::updateStatusLed() {
    if (!initialized_ || sleeping_) return;
    if (systemAlertActive_) {
        switch (systemAlertLevel_) {
        case semantic_display::AlertLevel::warning:
            statusLed_.blink({0.10f, 0.10f, 0.0f}, 2000u, 1000u);
            return;
        case semantic_display::AlertLevel::alarm:
            statusLed_.blink({0.50f, 0.0f, 0.0f}, 1000u, 500u);
            return;
        case semantic_display::AlertLevel::emergency:
            statusLed_.blink({1.0f, 1.0f, 1.0f}, 667u, 334u);
            return;
        case semantic_display::AlertLevel::info:
        default:
            break;
        }
    }
    if (alert_) {
        // Errors and alarms remain visible even when normal LEDs are disabled.
        statusLed_.blink({normalStatusLedDuty, 0.0f, 0.0f}, 500u, 175u);
        return;
    }
    const auto duty = statusLedDuty();
    if (duty == 0.0f) {
        statusLed_.off();
    } else if (starting_) {
        statusLed_.blink({0.0f, duty, 0.0f}, 150u, 75u);
    } else {
        statusLed_.setColor({0.0f, duty, 0.0f});
    }
}

void FeedbackService::updateSystemAlarmTone(std::uint32_t now) {
    if (!systemAlertAudible_ ||
        static_cast<std::int32_t>(now - nextSystemToneAt_) < 0) return;
    if (systemAlertLevel_ == semantic_display::AlertLevel::emergency) {
        const auto frequency = emergencyHighTone_ ? 2400u : 1800u;
        (void)buzzer_.play(frequency, emergencyToneHoldMs, 1.0f);
        emergencyHighTone_ = !emergencyHighTone_;
        nextSystemToneAt_ = now + emergencyToneStepMs;
        return;
    }
    (void)buzzer_.play(alarmToneFrequencyHz, alarmToneDurationMs, 1.0f);
    nextSystemToneAt_ = now + alarmTonePeriodMs;
}

void FeedbackService::run() {
    const auto now = runtime_.millis();
    updateSystemAlarmTone(now);
    if (sleepCueActive_ && !buzzer_.playing()) sleepCueActive_ = false;
    if (attentionCueActive_) {
        if (attentionCueBeeps_ < attentionBeepCount && !buzzer_.playing() &&
            static_cast<std::int32_t>(now - nextAttentionCueAt_) >= 0) {
            if (buzzer_.play(attentionBeepFrequencyHz, attentionBeepDurationMs,
                             attentionCueLevel_)) {
                ++attentionCueBeeps_;
                nextAttentionCueAt_ = now + attentionBeepDurationMs + attentionBeepGapMs;
            }
        }
        if (attentionCueBeeps_ == attentionBeepCount && !buzzer_.playing()) {
            attentionCueActive_ = false;
        }
    }
    if (wakeCueActive_) {
        if (wakeCueBeeps_ < wakeBeepCount && !buzzer_.playing() &&
            static_cast<std::int32_t>(now - nextWakeCueAt_) >= 0) {
            (void)buzzer_.play(wakeBeepFrequencyHz, wakeBeepDurationMs, beepLevel());
            ++wakeCueBeeps_;
            // Preserve audible spacing even if another component temporarily
            // delayed the cooperative application loop.
            nextWakeCueAt_ = now + wakeBeepDurationMs + wakeBeepGapMs;
        }
        if (wakeCueBeeps_ == wakeBeepCount && !buzzer_.playing()) wakeCueActive_ = false;
    }
}

} // namespace remote_a
