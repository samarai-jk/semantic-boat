#include "semantic_link/protocol.hpp"
#include <array>
#include <cassert>
#include <cstring>

int main() {
    using namespace semantic_link;

    const std::array<std::uint8_t, 9> payload{{0u, 1u, 2u, 0u, 3u, 4u, 5u, 0u, 6u}};
    std::array<std::uint8_t, maxEncodedFrameSize> frame{};
    const auto size = encode(MessageType::subscribe, 42u, payload.data(), payload.size(),
                             frame.data(), frame.size(), 0x5au);
    assert(size != 0u);
    assert(frame[size - 1u] == 0u);

    Decoder decoder;
    MessageView message{};
    DecodeResult result{};
    for (std::size_t index = 0u; index < size; ++index) result = decoder.push(frame[index], message);
    assert(result == DecodeResult::message);
    assert(message.type == MessageType::subscribe);
    assert(message.sequence == 42u);
    assert(message.flags == 0x5au);
    assert(message.payloadSize == payload.size());
    assert(std::memcmp(message.payload, payload.data(), payload.size()) == 0);

    auto corrupt = frame;
    corrupt[2] ^= 0x40u;
    result = DecodeResult::none;
    for (std::size_t index = 0u; index < size; ++index) result = decoder.push(corrupt[index], message);
    assert(result == DecodeResult::error);
    assert(decoder.errorCount() == 1u);

    std::array<std::uint8_t, maxPayloadSize> maximum{};
    for (std::size_t index = 0u; index < maximum.size(); ++index) {
        maximum[index] = static_cast<std::uint8_t>(index);
    }
    const auto maximumSize = encode(MessageType::valueText, 255u, maximum.data(), maximum.size(),
                                    frame.data(), frame.size());
    assert(maximumSize != 0u);
    result = DecodeResult::none;
    for (std::size_t index = 0u; index < maximumSize; ++index) {
        result = decoder.push(frame[index], message);
    }
    assert(result == DecodeResult::message);
    assert(message.payloadSize == maxPayloadSize);
    assert(std::memcmp(message.payload, maximum.data(), maximum.size()) == 0);

    return 0;
}
