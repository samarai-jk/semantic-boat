#include "semantic_link/protocol.hpp"

namespace semantic_link {
namespace {

std::size_t cobsEncode(const std::uint8_t* input, std::size_t size,
                       std::uint8_t* output, std::size_t capacity) {
    if (!output || capacity == 0u || (size != 0u && !input)) return 0u;
    std::size_t readIndex{};
    std::size_t writeIndex{1u};
    std::size_t codeIndex{};
    std::uint8_t code{1u};

    while (readIndex < size) {
        if (input[readIndex] == 0u) {
            if (codeIndex >= capacity) return 0u;
            output[codeIndex] = code;
            code = 1u;
            codeIndex = writeIndex++;
            if (writeIndex > capacity) return 0u;
            ++readIndex;
        } else {
            if (writeIndex >= capacity) return 0u;
            output[writeIndex++] = input[readIndex++];
            ++code;
            if (code == 0xffu) {
                if (codeIndex >= capacity) return 0u;
                output[codeIndex] = code;
                code = 1u;
                codeIndex = writeIndex++;
                if (writeIndex > capacity) return 0u;
            }
        }
    }
    if (codeIndex >= capacity) return 0u;
    output[codeIndex] = code;
    return writeIndex;
}

std::size_t cobsDecode(const std::uint8_t* input, std::size_t size,
                       std::uint8_t* output, std::size_t capacity) {
    if (!input || !output || size == 0u) return 0u;
    std::size_t readIndex{};
    std::size_t writeIndex{};
    while (readIndex < size) {
        const auto code = input[readIndex++];
        if (code == 0u) return 0u;
        const auto copySize = static_cast<std::size_t>(code - 1u);
        if (copySize > size - readIndex || copySize > capacity - writeIndex) return 0u;
        for (std::size_t i = 0u; i < copySize; ++i) output[writeIndex++] = input[readIndex++];
        if (code != 0xffu && readIndex < size) {
            if (writeIndex >= capacity) return 0u;
            output[writeIndex++] = 0u;
        }
    }
    return writeIndex;
}

} // namespace

std::uint16_t crc16Ccitt(const std::uint8_t* data, std::size_t size) {
    if (!data && size != 0u) return 0u;
    std::uint16_t crc{0xffffu};
    for (std::size_t index = 0u; index < size; ++index) {
        crc ^= static_cast<std::uint16_t>(data[index]) << 8u;
        for (std::uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = static_cast<std::uint16_t>((crc & 0x8000u) != 0u
                ? (crc << 1u) ^ 0x1021u : crc << 1u);
        }
    }
    return crc;
}

std::size_t encode(MessageType type, std::uint8_t sequence,
                   const std::uint8_t* payload, std::size_t payloadSize,
                   std::uint8_t* output, std::size_t outputCapacity,
                   std::uint8_t flags) {
    if (!output || payloadSize > maxPayloadSize ||
        (payloadSize != 0u && !payload)) return 0u;

    std::array<std::uint8_t, maxDecodedFrameSize> decoded{};
    decoded[0] = protocolVersion;
    decoded[1] = static_cast<std::uint8_t>(type);
    decoded[2] = sequence;
    decoded[3] = flags;
    writeU16(decoded.data() + 4u, static_cast<std::uint16_t>(payloadSize));
    for (std::size_t index = 0u; index < payloadSize; ++index) {
        decoded[frameHeaderSize + index] = payload[index];
    }
    const auto crcOffset = frameHeaderSize + payloadSize;
    writeU16(decoded.data() + crcOffset, crc16Ccitt(decoded.data(), crcOffset));
    const auto encodedSize = cobsEncode(decoded.data(), crcOffset + frameCrcSize,
                                        output, outputCapacity);
    if (encodedSize == 0u || encodedSize >= outputCapacity) return 0u;
    output[encodedSize] = 0u;
    return encodedSize + 1u;
}

void Decoder::reset() {
    encodedSize_ = 0u;
    discardUntilDelimiter_ = false;
}

DecodeResult Decoder::push(std::uint8_t byte, MessageView& message) {
    if (byte == 0u) {
        if (discardUntilDelimiter_) {
            reset();
            return DecodeResult::error;
        }
        if (encodedSize_ == 0u) return DecodeResult::none;
        return finish(message);
    }
    if (discardUntilDelimiter_) return DecodeResult::none;
    if (encodedSize_ >= encoded_.size()) {
        discardUntilDelimiter_ = true;
        ++errorCount_;
        return DecodeResult::error;
    }
    encoded_[encodedSize_++] = byte;
    return DecodeResult::none;
}

DecodeResult Decoder::finish(MessageView& message) {
    const auto decodedSize = cobsDecode(encoded_.data(), encodedSize_,
                                        decoded_.data(), decoded_.size());
    encodedSize_ = 0u;
    if (decodedSize < frameHeaderSize + frameCrcSize || decoded_[0] != protocolVersion) {
        ++errorCount_;
        return DecodeResult::error;
    }
    const auto payloadSize = readU16(decoded_.data() + 4u);
    const auto expectedSize = frameHeaderSize + payloadSize + frameCrcSize;
    if (payloadSize > maxPayloadSize || decodedSize != expectedSize) {
        ++errorCount_;
        return DecodeResult::error;
    }
    const auto expectedCrc = readU16(decoded_.data() + decodedSize - frameCrcSize);
    const auto actualCrc = crc16Ccitt(decoded_.data(), decodedSize - frameCrcSize);
    if (expectedCrc != actualCrc) {
        ++errorCount_;
        return DecodeResult::error;
    }
    message.type = static_cast<MessageType>(decoded_[1]);
    message.sequence = decoded_[2];
    message.flags = decoded_[3];
    message.payload = decoded_.data() + frameHeaderSize;
    message.payloadSize = payloadSize;
    return DecodeResult::message;
}

} // namespace semantic_link
