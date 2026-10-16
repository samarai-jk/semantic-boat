#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace semantic_link {

inline constexpr std::uint8_t protocolVersion = 1u;
inline constexpr std::uint8_t addressedProtocolVersion = 2u;
inline constexpr std::uint32_t replyStartWindowMs = 100u;
inline constexpr std::size_t frameHeaderSize = 6u;
inline constexpr std::size_t frameCrcSize = 2u;
inline constexpr std::size_t maxPayloadSize = 240u;
inline constexpr std::size_t maxDecodedFrameSize =
    frameHeaderSize + 1u + maxPayloadSize + frameCrcSize;
inline constexpr std::size_t maxEncodedFrameSize =
    maxDecodedFrameSize + maxDecodedFrameSize / 254u + 2u;

enum class MessageType : std::uint8_t {
    hello = 0x01u,
    subscriptionsBegin = 0x02u,
    subscribe = 0x03u,
    subscriptionsEnd = 0x04u,
    valueNumber = 0x10u,
    valueText = 0x11u,
    valueUnavailable = 0x12u,
    ping = 0x20u,
    pong = 0x21u,
    configListRequest = 0x30u,
    configListBegin = 0x31u,
    configListItem = 0x32u,
    configListEnd = 0x33u,
    configRequest = 0x34u,
    configBegin = 0x35u,
    configChunk = 0x36u,
    configEnd = 0x37u,
    alertUpdate = 0x40u,
    alertRemove = 0x41u,
    alertAction = 0x42u,
    alertSilence = 0x43u,
    busPoll = 0x50u,
    busIdle = 0x51u,
};

enum class AlertAction : std::uint8_t {
    acknowledge = 0u,
    snooze = 1u,
    silence = 2u,
};

enum class DecodeResult : std::uint8_t { none, message, error };

struct MessageView {
    MessageType type{};
    std::uint8_t sequence{};
    std::uint8_t flags{};
    const std::uint8_t* payload{};
    std::uint16_t payloadSize{};
    std::uint8_t version{protocolVersion};
    bool response{};
};

std::uint16_t crc16Ccitt(const std::uint8_t* data, std::size_t size);

std::size_t encode(MessageType type, std::uint8_t sequence,
                   const std::uint8_t* payload, std::size_t payloadSize,
                   std::uint8_t* output, std::size_t outputCapacity,
                   std::uint8_t flags = 0u,
                   std::uint8_t version = protocolVersion, bool response = false);

class Decoder {
public:
    DecodeResult push(std::uint8_t byte, MessageView& message);
    void reset();
    std::uint32_t errorCount() const { return errorCount_; }

private:
    DecodeResult finish(MessageView& message);

    std::array<std::uint8_t, maxEncodedFrameSize> encoded_{};
    std::array<std::uint8_t, maxDecodedFrameSize> decoded_{};
    std::size_t encodedSize_{};
    std::uint32_t errorCount_{};
    bool discardUntilDelimiter_{};
};

class PolledEndpoint {
public:
    void setAddress(std::uint8_t address);
    void clearQueue();
    bool enqueue(MessageType type, const std::uint8_t* payload, std::size_t size);
    bool request(const MessageView& message, std::uint32_t now, std::uint32_t receivedAt);
    bool duplicate() const { return duplicate_; }
    std::size_t reply(std::uint32_t now, std::uint8_t* output, std::size_t capacity);

private:
    struct Pending {
        MessageType type{};
        std::uint16_t size{};
        std::array<std::uint8_t, maxPayloadSize> payload{};
    };
    std::array<Pending, 4u> queue_{};
    std::array<std::uint8_t, maxEncodedFrameSize> previousReply_{};
    std::size_t previousReplySize_{};
    std::uint32_t previousReplyAt_{};
    std::uint32_t grantedAt_{};
    std::uint16_t previousRequestCrc_{};
    std::uint16_t requestCrc_{};
    MessageType previousType_{};
    MessageType requestType_{};
    std::uint8_t address_{1u};
    std::uint8_t head_{};
    std::uint8_t count_{};
    std::uint8_t sequence_{};
    std::uint8_t previousSequence_{};
    bool duplicate_{};
    bool granted_{};
};

inline std::uint16_t readU16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8u);
}

inline std::uint32_t readU32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8u) |
           (static_cast<std::uint32_t>(data[2]) << 16u) |
           (static_cast<std::uint32_t>(data[3]) << 24u);
}

inline void writeU16(std::uint8_t* data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8u);
}

inline void writeU32(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8u);
    data[2] = static_cast<std::uint8_t>(value >> 16u);
    data[3] = static_cast<std::uint8_t>(value >> 24u);
}

} // namespace semantic_link
