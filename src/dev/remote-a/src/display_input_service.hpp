#pragma once

#include "feedback_service.hpp"
#include "semantic_display/display.hpp"
#include "slstm32/drivers/button.hpp"
#include "slstm32/event_bus.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace remote_a {

class DisplayInputService final : public slstm32::Service {
public:
    DisplayInputService(slstm32::Runtime runtime, slstm32::EventBus& events,
                        slstm32::drivers::ButtonDriver& buttons,
                        semantic_display::DisplayService& display,
                        FeedbackService& feedback)
        : runtime_(runtime), events_(events), buttons_(buttons), display_(display),
          feedback_(feedback) {}
    bool init() override;
    void run() override;

private:
    static void eventThunk(slstm32::EventId, const void*, std::uint8_t, void*);
    void onButton(std::uint8_t id);
    slstm32::Runtime runtime_;
    slstm32::EventBus& events_;
    slstm32::drivers::ButtonDriver& buttons_;
    semantic_display::DisplayService& display_;
    FeedbackService& feedback_;
    std::uint32_t upPressedAt_{};
    bool upHoldPending_{};
    bool wakeReleasePending_{};
    std::uint8_t upHoldStage_{};
    std::uint8_t wakeButtonId_{};
};

} // namespace remote_a
