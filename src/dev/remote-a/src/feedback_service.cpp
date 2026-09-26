#include "feedback_service.hpp"
#include "events.hpp"
#include "slstm32/drivers/button.hpp"

namespace remote_a {

bool FeedbackService::init() {
    if (!runtime_.millis || !events_.subscribe(buttonPressed, &FeedbackService::eventThunk, this)) {
        return false;
    }
    statusLed_.blink({0.0f, 0.05f, 0.0f}, 150u, 75u);
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
    signalLed_.pulse(40u);
    (void)buzzer_.play(2500u, 40u);
}

void FeedbackService::run() {
    if (!starting_ || static_cast<std::int32_t>(runtime_.millis() - startupEndsAt_) < 0) return;
    starting_ = false;
    statusLed_.setColor({0.0f, 0.05f, 0.0f});
}

} // namespace remote_a
