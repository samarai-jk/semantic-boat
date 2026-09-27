#include "display_input_service.hpp"
#include "events.hpp"
#include "slstm32/drivers/button.hpp"

namespace remote_a {
namespace {

constexpr std::uint32_t releaseSettleMs = 50u;
constexpr std::uint32_t sleepPromptMs = 1000u;
constexpr std::uint32_t sleepCountdownMs = 2000u;
constexpr std::uint32_t sleepHoldMs = 3000u;
constexpr std::uint32_t setupHoldMs = 1000u;

} // namespace

bool DisplayInputService::init() {
    return runtime_.millis &&
        events_.subscribe(buttonPressed, &DisplayInputService::eventThunk, this);
}

void DisplayInputService::eventThunk(slstm32::EventId, const void* payload,
                                     std::uint8_t size, void* context) {
    if (!payload || size != sizeof(slstm32::drivers::ButtonEvent) || !context) return;
    const auto& event = *static_cast<const slstm32::drivers::ButtonEvent*>(payload);
    if (!event.pressed) return;
    auto& self = *static_cast<DisplayInputService*>(context);
    self.onButton(event.id);
}

void DisplayInputService::onButton(std::uint8_t id) {
    if (display_.sleepActive()) {
        upHoldPending_ = false;
        downHoldPending_ = false;
        upHoldStage_ = 0u;
        if (display_.wake()) {
            // Consume the whole physical wake press. In particular, do not
            // clear debounce state here: doing so lets contact bounce become a
            // second normal button press after the blocking panel wake-up.
            wakeButtonId_ = id;
            wakeReleasePending_ = true;
            display_.dismissTransientModal();
            feedback_.setSleeping(false);
        }
        return;
    }
    if (wakeReleasePending_) return;

    using semantic_display::InputAction;
    if (display_.alertVisible()) {
        // Alerts own the four horizontal controls and suppress both long-press
        // device gestures. Vertical buttons are deliberately inert here.
        upHoldPending_ = false;
        downHoldPending_ = false;
        upHoldStage_ = 0u;
        if (id == 2u) display_.handle(InputAction::previousPage);
        else if (id == 3u) display_.handle(InputAction::nextPage);
        else if (id == 4u) display_.handle(InputAction::action1);
        else if (id == 5u) display_.handle(InputAction::action2);
        return;
    }
    if (display_.selectionVisible() && (id == 0u || id == 1u)) {
        // Selection lists use the vertical buttons only for cursor movement.
        // Act on the press edge so the sleep/configuration hold gestures do
        // not add a one-second delay to each move.
        upHoldPending_ = false;
        downHoldPending_ = false;
        upHoldStage_ = 0u;
        display_.handle(id == 0u ? InputAction::previousSection
                                 : InputAction::nextSection);
        return;
    }
    if (id != 0u && upHoldPending_) {
        upHoldPending_ = false;
        upHoldStage_ = 0u;
        display_.dismissTransientModal();
    }
    if (id != 1u) downHoldPending_ = false;
    switch (id) {
    case 0u:
        upPressedAt_ = runtime_.millis();
        upHoldPending_ = true;
        upHoldStage_ = 0u;
        break;
    case 1u:
        downPressedAt_ = runtime_.millis();
        downHoldPending_ = true;
        break;
    case 2u: display_.handle(InputAction::previousPage); break;
    case 3u: display_.handle(InputAction::nextPage); break;
    case 4u: display_.handle(InputAction::action1); break;
    case 5u: display_.handle(InputAction::action2); break;
    default: break;
    }
}

void DisplayInputService::run() {
    if (wakeReleasePending_) {
        if (!buttons_.pressed(wakeButtonId_)) wakeReleasePending_ = false;
        return;
    }
    if (display_.sleepActive()) return;
    if (downHoldPending_) {
        const auto elapsed = static_cast<std::uint32_t>(runtime_.millis() - downPressedAt_);
        if (elapsed >= releaseSettleMs && !buttons_.pressed(1u)) {
            downHoldPending_ = false;
            display_.handle(semantic_display::InputAction::nextSection);
        } else if (elapsed >= setupHoldMs && buttons_.pressed(1u)) {
            downHoldPending_ = false;
            if (actions_.openSetup) {
                actions_.openSetup(actions_.context);
            }
        }
    }
    if (!upHoldPending_) return;
    const auto elapsed = static_cast<std::uint32_t>(runtime_.millis() - upPressedAt_);
    if (elapsed < releaseSettleMs) return;
    if (!buttons_.pressed(0u)) {
        upHoldPending_ = false;
        upHoldStage_ = 0u;
        display_.dismissTransientModal();
        display_.handle(semantic_display::InputAction::previousSection);
        return;
    }
    if (elapsed >= sleepPromptMs && upHoldStage_ == 0u) {
        display_.showTransientModal("SWITCH OFF", "2 ...",
                                    "KEEP DOWN TO SWITCH OFF");
        upHoldStage_ = 1u;
    }
    if (elapsed >= sleepCountdownMs && upHoldStage_ == 1u) {
        display_.showTransientModal("SWITCH OFF", "1 ...",
                                    "KEEP DOWN TO SWITCH OFF");
        upHoldStage_ = 2u;
    }
    if (elapsed < sleepHoldMs) return;
    upHoldPending_ = false;
    upHoldStage_ = 0u;
    buttons_.resetDebounce();
    feedback_.setSleeping(true);
    display_.showTransientModal("SLEEPING", "",
                                "PRESS ANY BUTTON TO WAKE UP");
    display_.requestSleep();
}

} // namespace remote_a
