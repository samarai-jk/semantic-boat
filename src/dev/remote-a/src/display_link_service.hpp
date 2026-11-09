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

// Firmware update requests (semantic-link PROTOCOL.md, "Firmware update").
struct FirmwareMessageReceiver {
    void* context{};
    // Writes the firmwareStatus payload; returns its size.
    std::size_t (*status)(void* context, std::uint8_t* output, std::size_t capacity){};
    void (*enterBootloader)(void* context){};
};

// Graph history loaded by the gateway (semantic-link PROTOCOL.md, "History").
struct HistoryMessageReceiver {
    void* context{};
    void (*data)(void* context, std::uint16_t requestId, std::uint16_t sourceIndex, std::uint32_t intervalMs,
                 std::uint16_t total, std::uint16_t first, const float* values, std::size_t count){};
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
    void setFirmwareReceiver(FirmwareMessageReceiver receiver) { firmwareReceiver_ = receiver; }
    void setHistoryReceiver(HistoryMessageReceiver receiver) { historyReceiver_ = receiver; }
    // The framebuffer read by screenshots (semantic-link PROTOCOL.md, "Screenshot").
    void setScreen(const std::uint8_t* frame, std::uint16_t width, std::uint16_t height, std::uint8_t rotation) {
        screen_ = frame; screenWidth_ = width; screenHeight_ = height; screenRotation_ = rotation;
    }
    bool requestHistory(std::uint16_t requestId, std::uint16_t sourceIndex, std::uint32_t intervalMs,
                        std::uint16_t points, std::uint8_t reducer);
    bool requestConfigList();
    bool requestConfig(std::string_view name);
    bool sendAlertAction(semantic_link::AlertAction action, std::uint32_t occurrence,
                         std::string_view id);
    void packageChanged();
    void unsubscribe();
    void shutdown();
    bool connected() const { return peerSeen_; }
    std::uint32_t droppedBytes() const { return transport_.droppedBytes(); }
    std::uint32_t receiveErrors() const { return transport_.receiveErrors(); }
    std::uint32_t lastReceiveError() const { return transport_.lastReceiveError(); }
    std::uint32_t decodeErrors() const { return decoder_.errorCount(); }

private:
    bool send(semantic_link::MessageType type, const std::uint8_t* payload = nullptr,
              std::size_t payloadSize = 0u);
    void sendHello();
    void sendSubscriptions();
    void pumpSubscriptions();
    void handle(const semantic_link::MessageView& message);
    void sendScreen(std::uint32_t offset);
    bool accepts(std::uint16_t sourceIndex) const;

    slstm32::Runtime runtime_;
    UartTransport& transport_;
    semantic_display::PackageView& package_;
    semantic_display::DataStore& data_;
    semantic_display::DisplayService& display_;
    semantic_link::Decoder decoder_{};
    semantic_link::PolledEndpoint endpoint_{};
    semantic_display::RecordView subscriptionRecord_{};
    std::uint16_t subscriptionCount_{};
    std::uint8_t subscriptionStage_{};
    bool subscriptionRecordValid_{};
    std::uint8_t address_{};
    ConfigMessageReceiver configReceiver_{};
    AlertMessageReceiver alertReceiver_{};
    FirmwareMessageReceiver firmwareReceiver_{};
    HistoryMessageReceiver historyReceiver_{};
    const std::uint8_t* screen_{};
    std::uint16_t screenWidth_{};
    std::uint16_t screenHeight_{};
    std::uint8_t screenRotation_{};
    std::uint16_t subscribedSection_{semantic_display::noIndex};
    std::uint32_t lastAnnouncementAt_{};
    std::uint32_t lastPeerAt_{};
    bool peerSeen_{};
    bool shuttingDown_{};
    std::uint8_t sequence_{};
};

} // namespace remote_a
