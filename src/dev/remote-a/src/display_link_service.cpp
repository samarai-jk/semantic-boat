#include "display_link_service.hpp"
#include <array>
#include <cmath>
#include <cstring>

namespace remote_a {
namespace {

constexpr std::uint8_t displayRole = 1u;
constexpr char deviceName[] = "remote-a";
constexpr std::uint8_t permanentSourceFlag = 0x01u;
constexpr std::uint8_t ageSupportedFlag = 0x02u;
constexpr std::uint32_t announcementRetryMs = 1000u;
constexpr std::uint32_t peerTimeoutMs = 12000u;
constexpr std::string_view localProvider{"device"};

} // namespace

bool DisplayLinkService::init() {
    if (!runtime_.millis || !package_.valid()) return false;
    address_ = transport_.address(); endpoint_.setAddress(address_);
    sendHello();
    sendSubscriptions();
    lastAnnouncementAt_ = runtime_.millis();
    return true;
}

void DisplayLinkService::transportRestored() {
    decoder_.reset();
    peerSeen_ = false;
    lastPeerAt_ = 0u;
    sendHello();
    sendSubscriptions();
    lastAnnouncementAt_ = runtime_.millis();
}

bool DisplayLinkService::requestConfigList() {
    return send(semantic_link::MessageType::configListRequest);
}

bool DisplayLinkService::requestConfig(std::string_view name) {
    if (name.empty() || name.size() > 0xffu || name.size() + 1u >
        semantic_link::maxPayloadSize) return false;
    std::array<std::uint8_t, semantic_link::maxPayloadSize> payload{};
    payload[0] = static_cast<std::uint8_t>(name.size());
    std::memcpy(payload.data() + 1u, name.data(), name.size());
    return send(semantic_link::MessageType::configRequest,
                payload.data(), name.size() + 1u);
}

bool DisplayLinkService::sendAlertAction(semantic_link::AlertAction action,
                                         std::uint32_t occurrence,
                                         std::string_view id) {
    if (id.empty() || id.size() > 0xffu || id.size() + 6u >
        semantic_link::maxPayloadSize) return false;
    std::array<std::uint8_t, semantic_link::maxPayloadSize> payload{};
    payload[0] = static_cast<std::uint8_t>(action);
    semantic_link::writeU32(payload.data() + 1u, occurrence);
    payload[5] = static_cast<std::uint8_t>(id.size());
    std::memcpy(payload.data() + 6u, id.data(), id.size());
    return send(semantic_link::MessageType::alertAction,
                payload.data(), id.size() + 6u);
}

void DisplayLinkService::packageChanged() {
    if (shuttingDown_) return;
    subscribedSection_ = semantic_display::noIndex;
    sendSubscriptions();
}

void DisplayLinkService::unsubscribe() {
    // An empty snapshot atomically replaces the server's active subscriptions.
    std::uint8_t begin[4]{};
    semantic_link::writeU16(begin, semantic_display::noIndex);
    semantic_link::writeU16(begin + 2u, 0u);
    (void)send(semantic_link::MessageType::subscriptionsBegin, begin, sizeof begin);
    (void)send(semantic_link::MessageType::subscriptionsEnd);
    subscribedSection_ = semantic_display::noIndex;
}

void DisplayLinkService::shutdown() {
    if (shuttingDown_) return;
    unsubscribe();
    shuttingDown_ = true;
    peerSeen_ = false;
}

bool DisplayLinkService::send(semantic_link::MessageType type,
                              const std::uint8_t* payload, std::size_t payloadSize) {
    if (transport_.addressed()) return endpoint_.enqueue(type, payload, payloadSize);
    std::array<std::uint8_t, semantic_link::maxEncodedFrameSize> frame{};
    const auto size = semantic_link::encode(type, sequence_++, payload, payloadSize,
                                            frame.data(), frame.size());
    return size != 0u && transport_.write(frame.data(), size);
}

void DisplayLinkService::sendHello() {
    std::array<std::uint8_t, 2u + sizeof deviceName - 1u> payload{};
    payload[0] = displayRole;
    payload[1] = sizeof deviceName - 1u;
    std::memcpy(payload.data() + 2u, deviceName, sizeof deviceName - 1u);
    (void)send(semantic_link::MessageType::hello, payload.data(), payload.size());
}

void DisplayLinkService::sendSubscriptions() {
    const auto section = display_.activeSection();
    std::uint16_t count{};
    semantic_display::RecordView record{};
    if (package_.first(record)) {
        do {
            semantic_display::SourceView source{};
            if (!package_.source(record, source) ||
                source.provider == localProvider ||
                !semantic_display::sourceIsSubscribed(source, section)) continue;
            const auto payloadSize = 9u + source.provider.size() + source.path.size();
            if (source.provider.size() <= 0xffu && source.path.size() <= 0xffu &&
                payloadSize <= semantic_link::maxPayloadSize) ++count;
        } while (package_.next(record));
    }

    subscribedSection_ = section;
    subscriptionCount_ = count;
    subscriptionStage_ = 1u;
    pumpSubscriptions();
}

void DisplayLinkService::pumpSubscriptions() {
    if (subscriptionStage_ == 1u) {
        std::uint8_t begin[4]{};
        semantic_link::writeU16(begin, subscribedSection_);
        semantic_link::writeU16(begin + 2u, subscriptionCount_);
        if (!send(semantic_link::MessageType::subscriptionsBegin, begin, sizeof begin)) return;
        subscriptionRecordValid_ = package_.first(subscriptionRecord_);
        subscriptionStage_ = 2u;
    }
    if (subscriptionStage_ == 2u) {
        while (subscriptionRecordValid_) {
            semantic_display::SourceView source{};
            if (!package_.source(subscriptionRecord_, source) ||
                source.provider == localProvider ||
                !semantic_display::sourceIsSubscribed(source, subscribedSection_)) {
                subscriptionRecordValid_ = package_.next(subscriptionRecord_); continue;
            }
            const auto payloadSize = 9u + source.provider.size() + source.path.size();
            if (source.provider.size() > 0xffu || source.path.size() > 0xffu ||
                payloadSize > semantic_link::maxPayloadSize) {
                subscriptionRecordValid_ = package_.next(subscriptionRecord_); continue;
            }
            std::array<std::uint8_t, semantic_link::maxPayloadSize> payload{};
            semantic_link::writeU16(payload.data(), source.sourceIndex);
            semantic_link::writeU32(payload.data() + 2u, source.subscribePeriodMs);
            payload[6] = static_cast<std::uint8_t>((source.permanent ? permanentSourceFlag : 0u) | ageSupportedFlag);
            payload[7] = static_cast<std::uint8_t>(source.provider.size());
            payload[8] = static_cast<std::uint8_t>(source.path.size());
            std::memcpy(payload.data() + 9u, source.provider.data(), source.provider.size());
            std::memcpy(payload.data() + 9u + source.provider.size(),
                        source.path.data(), source.path.size());
            if (!send(semantic_link::MessageType::subscribe, payload.data(), payloadSize)) return;
            subscriptionRecordValid_ = package_.next(subscriptionRecord_);
        }
        subscriptionStage_ = 3u;
    }
    if (subscriptionStage_ == 3u && send(semantic_link::MessageType::subscriptionsEnd)) subscriptionStage_ = 0u;
}

bool DisplayLinkService::accepts(std::uint16_t sourceIndex) const {
    semantic_display::SourceView source{};
    return sourceIndex < data_.size() && package_.sourceByIndex(sourceIndex, source) &&
        source.provider != localProvider &&
        semantic_display::sourceIsSubscribed(source, display_.activeSection());
}

void DisplayLinkService::handle(const semantic_link::MessageView& message) {
    const auto* payload = message.payload;
    const auto size = message.payloadSize;
    const auto firstPeerFrame = !peerSeen_;
    peerSeen_ = true;
    lastPeerAt_ = runtime_.millis();
    if (firstPeerFrame) {
        // The gateway may have attached after our one-shot startup frames were
        // sent. Re-announce the complete state for any first valid peer frame,
        // not only Hello, so reconnects cannot remain silently unsubscribed.
        sendHello();
        sendSubscriptions();
    }
    if (message.type == semantic_link::MessageType::hello) {
        if (!firstPeerFrame) {
            sendHello();
            sendSubscriptions();
        }
        return;
    }
    if (message.type == semantic_link::MessageType::ping && size == 4u) {
        (void)send(semantic_link::MessageType::pong, payload, size);
        return;
    }
    if (message.type == semantic_link::MessageType::configListBegin && size == 4u) {
        if (configReceiver_.listBegin) {
            configReceiver_.listBegin(configReceiver_.context,
                semantic_link::readU16(payload), semantic_link::readU16(payload + 2u));
        }
        return;
    }
    if (message.type == semantic_link::MessageType::configListItem && size >= 5u &&
        payload[4] == size - 5u) {
        if (configReceiver_.listItem) {
            configReceiver_.listItem(configReceiver_.context,
                semantic_link::readU16(payload), semantic_link::readU16(payload + 2u),
                {reinterpret_cast<const char*>(payload + 5u), payload[4]});
        }
        return;
    }
    if (message.type == semantic_link::MessageType::configListEnd && size == 2u) {
        if (configReceiver_.listEnd) {
            configReceiver_.listEnd(configReceiver_.context,
                                    semantic_link::readU16(payload));
        }
        return;
    }
    if (message.type == semantic_link::MessageType::configBegin && size >= 11u &&
        payload[10] == size - 11u) {
        if (configReceiver_.configBegin) {
            configReceiver_.configBegin(configReceiver_.context,
                semantic_link::readU16(payload), semantic_link::readU32(payload + 2u),
                semantic_link::readU32(payload + 6u),
                {reinterpret_cast<const char*>(payload + 11u), payload[10]});
        }
        return;
    }
    if (message.type == semantic_link::MessageType::configChunk && size >= 6u) {
        if (configReceiver_.configChunk) {
            configReceiver_.configChunk(configReceiver_.context,
                semantic_link::readU16(payload), semantic_link::readU32(payload + 2u),
                payload + 6u, size - 6u);
        }
        return;
    }
    if (message.type == semantic_link::MessageType::configEnd && size == 2u) {
        if (configReceiver_.configEnd) {
            configReceiver_.configEnd(configReceiver_.context,
                                      semantic_link::readU16(payload));
        }
        return;
    }
    if (message.type == semantic_link::MessageType::alertUpdate && size >= 9u) {
        const auto idSize = payload[6];
        const auto titleSize = payload[7];
        const auto messageSize = payload[8];
        const auto stringsSize = static_cast<std::size_t>(idSize) + titleSize + messageSize;
        if (payload[0] <= 3u && stringsSize == size - 9u && idSize != 0u &&
            alertReceiver_.update) {
            const auto* text = reinterpret_cast<const char*>(payload + 9u);
            alertReceiver_.update(alertReceiver_.context, payload[0],
                semantic_link::readU32(payload + 2u),
                {text, idSize}, {text + idSize, titleSize},
                {text + idSize + titleSize, messageSize});
        }
        return;
    }
    if ((message.type == semantic_link::MessageType::alertRemove ||
         message.type == semantic_link::MessageType::alertSilence) &&
        size >= 2u && payload[0] == size - 1u && payload[0] != 0u) {
        const std::string_view id{reinterpret_cast<const char*>(payload + 1u), payload[0]};
        if (message.type == semantic_link::MessageType::alertRemove) {
            if (alertReceiver_.remove) alertReceiver_.remove(alertReceiver_.context, id);
        } else if (alertReceiver_.silence) {
            alertReceiver_.silence(alertReceiver_.context, id);
        }
        return;
    }
    if (size < 2u) return;
    const auto sourceIndex = semantic_link::readU16(payload);
    if (!accepts(sourceIndex)) return;
    const auto now = runtime_.millis();
    bool updated{};
    if (message.type == semantic_link::MessageType::valueNumber && (size == 6u || size == 10u)) {
        const auto raw = semantic_link::readU32(payload + 2u);
        float value{};
        static_assert(sizeof value == sizeof raw, "Semantic Link requires binary32 float");
        std::memcpy(&value, &raw, sizeof value);
        const auto updatedAt = size == 10u ? now - semantic_link::readU32(payload + 6u) : now;
        updated = std::isfinite(value) && data_.setNumber(sourceIndex, value, updatedAt);
    } else if (message.type == semantic_link::MessageType::valueText && size >= 3u &&
               (payload[2] == size - 3u || payload[2] + 4u == size - 3u)) {
        const auto updatedAt = payload[2] == size - 3u ? now : now - semantic_link::readU32(payload + 3u + payload[2]);
        updated = data_.setText(sourceIndex,
            {reinterpret_cast<const char*>(payload + 3u), payload[2]}, updatedAt);
    } else if (message.type == semantic_link::MessageType::valueUnavailable && size == 2u) {
        updated = data_.setUnavailable(sourceIndex, now);
    }
    if (updated) display_.sourceUpdated(sourceIndex);
}

void DisplayLinkService::run() {
    if (shuttingDown_) return;
    const auto now = runtime_.millis();
    if (address_ != transport_.address()) {
        address_ = transport_.address(); endpoint_.setAddress(address_); transportRestored();
    }
    if (peerSeen_ && static_cast<std::uint32_t>(now - lastPeerAt_) >= peerTimeoutMs) {
        peerSeen_ = false;
        lastAnnouncementAt_ = now - announcementRetryMs;
    }
    if (display_.activeSection() != subscribedSection_) sendSubscriptions();
    pumpSubscriptions();
    if (!peerSeen_ &&
        static_cast<std::uint32_t>(now - lastAnnouncementAt_) >= announcementRetryMs) {
        if (!transport_.addressed()) { sendHello(); sendSubscriptions(); }
        lastAnnouncementAt_ = now;
    }
    std::uint8_t bytes[64]{};
    std::uint32_t receivedAt[64]{};
    const auto count = transport_.read(bytes, sizeof bytes, receivedAt);
    for (std::size_t index = 0u; index < count; ++index) {
        semantic_link::MessageView message{};
        if (decoder_.push(bytes[index], message) == semantic_link::DecodeResult::message) {
            if (transport_.addressed()) {
                if (!endpoint_.request(message, runtime_.millis(), receivedAt[index])) continue;
                if (!endpoint_.duplicate()) {
                    if (message.type == semantic_link::MessageType::hello) endpoint_.clearQueue();
                    if (message.type != semantic_link::MessageType::busPoll) handle(message);
                    else { peerSeen_ = true; lastPeerAt_ = runtime_.millis(); }
                    pumpSubscriptions();
                }
                std::array<std::uint8_t, semantic_link::maxEncodedFrameSize> reply{};
                const auto size = endpoint_.reply(runtime_.millis(), reply.data(), reply.size());
                if (size != 0u) (void)transport_.write(reply.data(), size);
            } else if (message.version == semantic_link::protocolVersion) handle(message);
            if (shuttingDown_) return;
        }
    }
}

} // namespace remote_a
