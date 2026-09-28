#include "display_link_service.hpp"
#include <array>
#include <cmath>
#include <cstring>

namespace remote_a {
namespace {

constexpr std::uint8_t displayRole = 1u;
constexpr char deviceName[] = "remote-a";
constexpr std::uint8_t permanentSourceFlag = 0x01u;
constexpr std::uint32_t announcementRetryMs = 1000u;
constexpr std::uint32_t peerTimeoutMs = 12000u;
constexpr std::string_view localProvider{"device"};

} // namespace

bool DisplayLinkService::init() {
    if (!runtime_.millis || !package_.valid()) return false;
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
    subscribedSection_ = semantic_display::noIndex;
    sendSubscriptions();
}

bool DisplayLinkService::send(semantic_link::MessageType type,
                              const std::uint8_t* payload, std::size_t payloadSize) {
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

    std::uint8_t begin[4]{};
    semantic_link::writeU16(begin, section);
    semantic_link::writeU16(begin + 2u, count);
    (void)send(semantic_link::MessageType::subscriptionsBegin, begin, sizeof begin);

    if (package_.first(record)) {
        do {
            semantic_display::SourceView source{};
            if (!package_.source(record, source) ||
                source.provider == localProvider ||
                !semantic_display::sourceIsSubscribed(source, section)) continue;
            const auto payloadSize = 9u + source.provider.size() + source.path.size();
            if (source.provider.size() > 0xffu || source.path.size() > 0xffu ||
                payloadSize > semantic_link::maxPayloadSize) continue;
            std::array<std::uint8_t, semantic_link::maxPayloadSize> payload{};
            semantic_link::writeU16(payload.data(), source.sourceIndex);
            semantic_link::writeU32(payload.data() + 2u, source.subscribePeriodMs);
            payload[6] = source.permanent ? permanentSourceFlag : 0u;
            payload[7] = static_cast<std::uint8_t>(source.provider.size());
            payload[8] = static_cast<std::uint8_t>(source.path.size());
            std::memcpy(payload.data() + 9u, source.provider.data(), source.provider.size());
            std::memcpy(payload.data() + 9u + source.provider.size(),
                        source.path.data(), source.path.size());
            (void)send(semantic_link::MessageType::subscribe, payload.data(), payloadSize);
        } while (package_.next(record));
    }
    (void)send(semantic_link::MessageType::subscriptionsEnd);
    subscribedSection_ = section;
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
    if (message.type == semantic_link::MessageType::valueNumber && size == 6u) {
        const auto raw = semantic_link::readU32(payload + 2u);
        float value{};
        static_assert(sizeof value == sizeof raw, "Semantic Link requires binary32 float");
        std::memcpy(&value, &raw, sizeof value);
        updated = std::isfinite(value) && data_.setNumber(sourceIndex, value, now);
    } else if (message.type == semantic_link::MessageType::valueText && size >= 3u &&
               payload[2] == size - 3u) {
        updated = data_.setText(sourceIndex,
            {reinterpret_cast<const char*>(payload + 3u), payload[2]}, now);
    } else if (message.type == semantic_link::MessageType::valueUnavailable && size == 2u) {
        updated = data_.setUnavailable(sourceIndex, now);
    }
    if (updated) display_.sourceUpdated(sourceIndex);
}

void DisplayLinkService::run() {
    const auto now = runtime_.millis();
    if (peerSeen_ && static_cast<std::uint32_t>(now - lastPeerAt_) >= peerTimeoutMs) {
        peerSeen_ = false;
        lastAnnouncementAt_ = now - announcementRetryMs;
    }
    if (display_.activeSection() != subscribedSection_) sendSubscriptions();
    if (!peerSeen_ &&
        static_cast<std::uint32_t>(now - lastAnnouncementAt_) >= announcementRetryMs) {
        sendHello();
        sendSubscriptions();
        lastAnnouncementAt_ = now;
    }
    std::uint8_t bytes[64]{};
    const auto count = transport_.read(bytes, sizeof bytes);
    for (std::size_t index = 0u; index < count; ++index) {
        semantic_link::MessageView message{};
        if (decoder_.push(bytes[index], message) == semantic_link::DecodeResult::message) {
            handle(message);
        }
    }
}

} // namespace remote_a
