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
    // Firmware update (PROTOCOL.md, "Firmware update"): gateway requests,
    // each answered with firmwareStatus.
    firmwareQuery = 0x60u,
    firmwareStatus = 0x61u,
    firmwareEnterBootloader = 0x62u,
    firmwareBegin = 0x63u,
    firmwareData = 0x64u,
    firmwareEnd = 0x65u,
    firmwareRestart = 0x66u,
    // Graph history (PROTOCOL.md, "History"): the device asks for the
    // points of one history source, the gateway answers from its database.
    historyRequest = 0x70u,
    historyData = 0x71u,
};

// historyData: header bytes before the float32 values.
inline constexpr std::size_t historyDataHeaderBytes = 13u;
inline constexpr std::size_t historyValuesPerFrame = (maxPayloadSize - historyDataHeaderBytes) / 4u;

enum class FirmwareMode : std::uint8_t { application = 1u, bootloader = 2u };

// Status codes as in B1S (src/lib/b1s/FIRMWARE.md).
enum class FirmwareResult : std::uint8_t {
    done = 0u, unknown = 1u, invalidValue = 3u, busy = 5u, wrongBoard = 7u, tooLarge = 8u,
    flashError = 9u, unexpectedOffset = 10u, crcMismatch = 11u,
};

// Image bytes per firmwareData message (offset u32 + data, a multiple of 8).
inline constexpr std::size_t firmwareChunkBytes = 232u;
inline constexpr std::uint8_t firmwareUnconfirmed = 1u; // firmwareStatus flags

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

// firmwareStatus payload: result, mode, board u16, next offset u32, image
// CRC u32, version u32, flags, text length, text (version or the reason the
// bootloader stays). Returns the size, or 0 if it does not fit.
inline std::size_t encodeFirmwareStatus(std::uint8_t* output, std::size_t capacity, FirmwareResult result,
                                        FirmwareMode mode, std::uint16_t board, std::uint32_t nextOffset,
                                        std::uint32_t imageCrc, std::uint32_t version, std::uint8_t flags,
                                        const char* text) {
    std::size_t length = 0u;
    while (text && text[length] != '\0' && length < 64u) ++length;
    if (!output || capacity < 18u + length) return 0u;
    output[0] = static_cast<std::uint8_t>(result);
    output[1] = static_cast<std::uint8_t>(mode);
    writeU16(output + 2u, board);
    writeU32(output + 4u, nextOffset);
    writeU32(output + 8u, imageCrc);
    writeU32(output + 12u, version);
    output[16] = flags;
    output[17] = static_cast<std::uint8_t>(length);
    for (std::size_t index = 0u; index < length; ++index) output[18u + index] = static_cast<std::uint8_t>(text[index]);
    return 18u + length;
}

} // namespace semantic_link
