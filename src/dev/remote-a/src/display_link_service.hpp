#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include "semantic_display/data.hpp"
#include "semantic_display/display.hpp"
#include "semantic_display/package.hpp"
#include "semantic_link/protocol.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"
#include "uart_transport.hpp"

namespace remote_a {

struct ConfigMessageReceiver {
    void* context{};
    void (*listBegin)(void* context, std::uint16_t listId, std::uint16_t count){};
    void (*listItem)(void* context, std::uint16_t listId, std::uint16_t index,
                     std::string_view name){};
    void (*listEnd)(void* context, std::uint16_t listId){};
    void (*configBegin)(void* context, std::uint16_t transferId, std::uint32_t size,
                        std::uint32_t crc, std::string_view name){};
    void (*configChunk)(void* context, std::uint16_t transferId, std::uint32_t offset,
                        const std::uint8_t* data, std::size_t size){};
    void (*configEnd)(void* context, std::uint16_t transferId){};
};

struct AlertMessageReceiver {
    void* context{};
    void (*update)(void* context, std::uint8_t level, std::uint32_t occurrence,
                   std::string_view id, std::string_view title,
                   std::string_view message){};
    void (*remove)(void* context, std::string_view id){};
    void (*silence)(void* context, std::string_view id){};
};

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
    void setConfigReceiver(ConfigMessageReceiver receiver) { configReceiver_ = receiver; }
    void setAlertReceiver(AlertMessageReceiver receiver) { alertReceiver_ = receiver; }
    bool requestConfigList();
    bool requestConfig(std::string_view name);
    bool sendAlertAction(semantic_link::AlertAction action, std::uint32_t occurrence,
                         std::string_view id);
    void packageChanged();

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
    ConfigMessageReceiver configReceiver_{};
    AlertMessageReceiver alertReceiver_{};
    std::uint16_t subscribedSection_{semantic_display::noIndex};
    std::uint32_t lastAnnouncementAt_{};
    bool peerSeen_{};
    std::uint8_t sequence_{};
};

} // namespace remote_a
