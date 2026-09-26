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
constexpr std::uint32_t signalLedPulseMs = 1u;
constexpr float statusLedDuty = 0.01f;

} // namespace

bool FeedbackService::init() {
    if (!runtime_.millis || !events_.subscribe(buttonPressed, &FeedbackService::eventThunk, this)) {
        return false;
    }
    statusLed_.blink({0.0f, statusLedDuty, 0.0f}, 150u, 75u);
    startupEndsAt_ = runtime_.millis() + 450u;
    starting_ = true;
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
    if (sleeping_ || wakeCueActive_) return;

    signalLed_.pulse(signalLedPulseMs);
    (void)buzzer_.play(buttonBeepFrequencyHz, buttonBeepDurationMs);
}

void FeedbackService::setAlert(bool active) {
    alert_ = active;
    if (sleeping_) return;
    if (active) statusLed_.blink({statusLedDuty, 0.0f, 0.0f}, 500u, 175u);
    else statusLed_.setColor({0.0f, statusLedDuty, 0.0f});
}

void FeedbackService::setSleeping(bool active) {
    sleeping_ = active;
    starting_ = false;
    if (active) {
        statusLed_.setColor({});
        wakeCueActive_ = false;
        sleepCueActive_ = true;
        (void)buzzer_.play(sleepBeepFrequencyHz, sleepBeepDurationMs);
        return;
    }

    sleepCueActive_ = false;
    wakeCueActive_ = true;
    wakeCueBeeps_ = 1u;
    nextWakeCueAt_ = runtime_.millis() + wakeBeepDurationMs + wakeBeepGapMs;
    (void)buzzer_.play(wakeBeepFrequencyHz, wakeBeepDurationMs);
    if (alert_) {
        statusLed_.blink({statusLedDuty, 0.0f, 0.0f}, 500u, 175u);
    } else {
        statusLed_.setColor({0.0f, statusLedDuty, 0.0f});
    }
}

void FeedbackService::run() {
    const auto now = runtime_.millis();
    if (sleepCueActive_ && !buzzer_.playing()) sleepCueActive_ = false;
    if (wakeCueActive_) {
        if (wakeCueBeeps_ < wakeBeepCount && !buzzer_.playing() &&
            static_cast<std::int32_t>(now - nextWakeCueAt_) >= 0) {
            (void)buzzer_.play(wakeBeepFrequencyHz, wakeBeepDurationMs);
            ++wakeCueBeeps_;
            // Preserve audible spacing even if another component temporarily
            // delayed the cooperative application loop.
            nextWakeCueAt_ = now + wakeBeepDurationMs + wakeBeepGapMs;
        }
        if (wakeCueBeeps_ == wakeBeepCount && !buzzer_.playing()) wakeCueActive_ = false;
    }
    if (!starting_ || static_cast<std::int32_t>(runtime_.millis() - startupEndsAt_) < 0) return;
    starting_ = false;
    if (!alert_ && !sleeping_) statusLed_.setColor({0.0f, statusLedDuty, 0.0f});
}

} // namespace remote_a
