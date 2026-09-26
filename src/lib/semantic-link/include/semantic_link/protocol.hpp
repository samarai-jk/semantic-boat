#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace semantic_link {

inline constexpr std::uint8_t protocolVersion = 1u;
inline constexpr std::size_t frameHeaderSize = 6u;
inline constexpr std::size_t frameCrcSize = 2u;
inline constexpr std::size_t maxPayloadSize = 240u;
inline constexpr std::size_t maxDecodedFrameSize =
    frameHeaderSize + maxPayloadSize + frameCrcSize;
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
};

enum class DecodeResult : std::uint8_t { none, message, error };

struct MessageView {
    MessageType type{};
    std::uint8_t sequence{};
    std::uint8_t flags{};
    const std::uint8_t* payload{};
    std::uint16_t payloadSize{};
};

std::uint16_t crc16Ccitt(const std::uint8_t* data, std::size_t size);

std::size_t encode(MessageType type, std::uint8_t sequence,
                   const std::uint8_t* payload, std::size_t payloadSize,
                   std::uint8_t* output, std::size_t outputCapacity,
                   std::uint8_t flags = 0u);

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
