#include "b1s/channel_settings.hpp"

namespace b1s {

namespace {

using slstm32::storage::RedundantBlobStore;

} // namespace

// Items: 0 role; channel c: 1 + 3c location, 2 + 3c label, 3 + 3c overrides.
std::size_t ChannelSettings::itemOf(Text text, std::uint8_t channel) {
    switch (text) {
    case Text::role: return 0u;
    case Text::location: return 1u + 3u * channel;
    case Text::label: return 2u + 3u * channel;
    }
    return 0u;
}

RedundantBlobStore ChannelSettings::store(std::size_t item) {
    return RedundantBlobStore{storage_, offset_ + static_cast<std::uint32_t>(item * RedundantBlobStore::requiredBytes),
                              RedundantBlobStore::requiredBytes};
}

// Override record: publish, instance, source, period, raw period (uint16 each).
void ChannelSettings::load() {
    for (std::size_t channel = 0u; channel < slots_; ++channel) {
        std::uint8_t data[7]{};
        std::size_t size = 0u;
        if (store(3u + 3u * channel).load(data, sizeof data, size) && size == sizeof data) {
            overrides_[channel] = Override{data[0], data[1], data[2], static_cast<std::uint16_t>(data[3] | data[4] << 8u),
                                           static_cast<std::uint16_t>(data[5] | data[6] << 8u)};
        }
    }
}

bool ChannelSettings::writeOverride(std::uint8_t channel, const Override& value) {
    if (channel >= slots_) return false;
    const std::uint8_t data[7]{value.publish, value.standardInstance, value.standardSource,
                               static_cast<std::uint8_t>(value.period), static_cast<std::uint8_t>(value.period >> 8u),
                               static_cast<std::uint8_t>(value.rawPeriod), static_cast<std::uint8_t>(value.rawPeriod >> 8u)};
    if (!store(3u + 3u * channel).commit(data, sizeof data)) return false;
    overrides_[channel] = value;
    return true;
}

bool ChannelSettings::readText(Text text, std::uint8_t channel, char (&out)[maxTextLength + 1u]) {
    out[0] = '\0';
    if (text != Text::role && channel >= slots_) return false;
    // Payload: length, characters (an empty text is a stored value too).
    std::uint8_t data[1u + maxTextLength]{};
    std::size_t size = 0u;
    if (!store(itemOf(text, channel)).load(data, sizeof data, size) || size == 0u || data[0] != size - 1u) return false;
    for (std::size_t index = 0u; index < data[0]; ++index) out[index] = static_cast<char>(data[1u + index]);
    out[data[0]] = '\0';
    return true;
}

bool ChannelSettings::writeText(Text text, std::uint8_t channel, const char* value) {
    if (text != Text::role && channel >= slots_) return false;
    std::uint8_t data[1u + maxTextLength]{};
    std::size_t length = 0u;
    while (value && value[length] != '\0' && length < maxTextLength) {
        data[1u + length] = static_cast<std::uint8_t>(value[length]);
        ++length;
    }
    data[0] = static_cast<std::uint8_t>(length);
    return store(itemOf(text, channel)).commit(data, 1u + length);
}

} // namespace b1s
