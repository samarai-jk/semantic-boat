#pragma once

#include <cstdint>
#include "semantic_display/data.hpp"
#include "semantic_display/display.hpp"
#include "semantic_display/package.hpp"
#include "semantic_link/protocol.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"
#include "uart_transport.hpp"

namespace remote_a {

class DisplayLinkService final : public slstm32::Service {
public:
    DisplayLinkService(slstm32::Runtime runtime, UartTransport& transport,
                       semantic_display::PackageView& package,
                       semantic_display::DataStore& data,
                       semantic_display::DisplayService& display)
        : runtime_(runtime), transport_(transport), package_(package),
          data_(data), display_(display) {}

    bool init() override;
    void run() override;
    void transportRestored();

private:
    bool send(semantic_link::MessageType type, const std::uint8_t* payload = nullptr,
              std::size_t payloadSize = 0u);
    void sendHello();
    void sendSubscriptions();
    void handle(const semantic_link::MessageView& message);
    bool accepts(std::uint16_t sourceIndex) const;

    slstm32::Runtime runtime_;
    UartTransport& transport_;
    semantic_display::PackageView& package_;
    semantic_display::DataStore& data_;
    semantic_display::DisplayService& display_;
    semantic_link::Decoder decoder_{};
    std::uint16_t subscribedSection_{semantic_display::noIndex};
    std::uint32_t lastAnnouncementAt_{};
    bool peerSeen_{};
    std::uint8_t sequence_{};
};

} // namespace remote_a
