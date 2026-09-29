#pragma once

#include "display_link_service.hpp"
#include "feedback_service.hpp"
#include "semantic_display/display.hpp"
#include "slstm32/service.hpp"
#include "uart_transport.hpp"

namespace remote_a {

// Single entry point for an orderly device reset. Future shutdown causes must
// request the operation here so subscriptions, user feedback, the transport,
// and the E-paper controller are always left in a known state.
class DeviceShutdownService final : public slstm32::Service {
public:
    DeviceShutdownService(DisplayLinkService& link, UartTransport& transport,
                          semantic_display::DisplayService& display,
                          FeedbackService& feedback)
        : link_(link), transport_(transport), display_(display), feedback_(feedback) {}

    bool init() override { return true; }
    void run() override;
    void requestReset();
    bool active() const { return active_; }

private:
    DisplayLinkService& link_;
    UartTransport& transport_;
    semantic_display::DisplayService& display_;
    FeedbackService& feedback_;
    bool active_{};
};

} // namespace remote_a
