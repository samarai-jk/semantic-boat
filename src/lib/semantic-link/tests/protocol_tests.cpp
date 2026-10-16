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

    PolledEndpoint first;
    PolledEndpoint second;
    first.setAddress(1u); second.setAddress(2u);
    assert(first.enqueue(MessageType::hello, payload.data(), payload.size()));
    assert(first.reply(1u, frame.data(), frame.size()) == 0u);
    MessageView poll{};
    poll.version = addressedProtocolVersion;
    poll.type = MessageType::busPoll;
    poll.flags = 1u;
    poll.sequence = 10u;
    assert(first.request(poll, 100u, 100u));
    assert(!second.request(poll, 100u, 100u));
    const auto replySize = first.reply(100u, frame.data(), frame.size());
    assert(replySize != 0u);
    const auto savedReply = frame;
    for (std::size_t index = 0u; index < replySize; ++index) result = decoder.push(frame[index], message);
    assert(result == DecodeResult::message);
    assert(message.version == addressedProtocolVersion && message.flags == 1u && message.sequence == 10u);
    assert(message.response);
    assert(!first.request(message, 100u, 100u));
    assert(message.type == MessageType::hello);
    assert(first.request(poll, 300u, 300u) && first.duplicate());
    assert(first.reply(300u, frame.data(), frame.size()) == replySize);
    assert(std::memcmp(frame.data(), savedReply.data(), replySize) == 0);
    ++poll.sequence;
    assert(!first.request(poll, 500u, 300u));
    assert(first.reply(500u, frame.data(), frame.size()) == 0u);
    poll.version = protocolVersion;
    assert(!first.request(poll, 500u, 500u));
    poll.version = addressedProtocolVersion;
    assert(first.request(poll, 600u, 600u));
    assert(first.reply(750u, frame.data(), frame.size()) == 0u);
    assert(first.request(poll, 800u, 800u) && first.duplicate());
    const auto idleSize = first.reply(800u, frame.data(), frame.size());
    for (std::size_t index = 0u; index < idleSize; ++index) result = decoder.push(frame[index], message);
    assert(message.type == MessageType::busIdle);
    for (std::size_t index = 0u; index < 4u; ++index) assert(first.enqueue(MessageType::subscribe, maximum.data(), maximum.size()));
    assert(!first.enqueue(MessageType::subscribe, maximum.data(), maximum.size()));
    ++poll.sequence;
    assert(first.request(poll, 1000u, 1000u));
    const auto fullReply = first.reply(1000u, frame.data(), frame.size());
    for (std::size_t index = 0u; index < fullReply; ++index) result = decoder.push(frame[index], message);
    assert(result == DecodeResult::message && message.payloadSize == maxPayloadSize);
    assert(first.enqueue(MessageType::subscriptionsEnd, nullptr, 0u));
    first.clearQueue();
    poll.flags = 2u;
    assert(second.request(poll, 1100u, 1100u));
    assert(second.reply(1100u, frame.data(), frame.size()) != 0u);
    return 0;
}
