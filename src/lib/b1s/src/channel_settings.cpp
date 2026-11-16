#include "b1s/channel_settings.hpp"

namespace b1s {

using slstm32::storage::RedundantBlobStore;

// Items: 0 role. perChannel: channel c 1 + 3c location, 2 + 3c label, 3 + 3c
// overrides. packed: 1 + 2c location, 2 + 2c label, then the overrides of
// channels 5k..5k+4 in item 1 + 2 * slots + k.
std::size_t ChannelSettings::itemOf(Text text, std::uint8_t channel) const {
    const std::size_t stride = layout_ == Layout::packed ? 2u : 3u;
    switch (text) {
    case Text::role: return 0u;
    case Text::location: return 1u + stride * channel;
    case Text::label: return 2u + stride * channel;
    }
    return 0u;
}

std::size_t ChannelSettings::overrideItem(std::uint8_t channel) const {
    return layout_ == Layout::packed ? 1u + 2u * slots_ + channel / packedOverrides : 3u + 3u * channel;
}

RedundantBlobStore ChannelSettings::store(std::size_t item) {
    return RedundantBlobStore{storage_, offset_ + static_cast<std::uint32_t>(item * RedundantBlobStore::requiredBytes),
                              RedundantBlobStore::requiredBytes};
}

namespace {

// Override record: publish, instance, source, period, raw period (uint16
// each), on change. Records written before on change had 7 bytes.
constexpr std::size_t overrideBytes = 8u;
constexpr std::size_t overrideBytesWithoutOnChange = 7u;

ChannelSettings::Override decodeOverride(const std::uint8_t* data, std::size_t size) {
    return ChannelSettings::Override{data[0], data[1], data[2],
                                     size > overrideBytesWithoutOnChange ? data[7] : ChannelSettings::notSet,
                                     static_cast<std::uint16_t>(data[3] | data[4] << 8u),
                                     static_cast<std::uint16_t>(data[5] | data[6] << 8u)};
}

void encodeOverride(const ChannelSettings::Override& value, std::uint8_t* data) {
    data[0] = value.publish;
    data[1] = value.standardInstance;
    data[2] = value.standardSource;
    data[3] = static_cast<std::uint8_t>(value.period);
    data[4] = static_cast<std::uint8_t>(value.period >> 8u);
    data[5] = static_cast<std::uint8_t>(value.rawPeriod);
    data[6] = static_cast<std::uint8_t>(value.rawPeriod >> 8u);
    data[7] = value.onChange;
}

} // namespace

void ChannelSettings::load() {
    static_assert(packedOverrides * overrideBytes <= RedundantBlobStore::maxPayloadBytes, "packed overrides fit an item");
    if (layout_ == Layout::packed) {
        for (std::size_t first = 0u; first < slots_; first += packedOverrides) {
            std::uint8_t data[packedOverrides * overrideBytes]{};
            std::size_t size = 0u;
            if (!store(overrideItem(static_cast<std::uint8_t>(first))).load(data, sizeof data, size)) continue;
            for (std::size_t index = 0u; index < packedOverrides && first + index < slots_; ++index) {
                if ((index + 1u) * overrideBytes <= size) {
                    overrides_[first + index] = decodeOverride(data + index * overrideBytes, overrideBytes);
                }
            }
        }
        return;
    }
    for (std::size_t channel = 0u; channel < slots_; ++channel) {
        std::uint8_t data[overrideBytes]{};
        std::size_t size = 0u;
        if (store(overrideItem(static_cast<std::uint8_t>(channel))).load(data, sizeof data, size) &&
            (size == overrideBytes || size == overrideBytesWithoutOnChange)) {
            overrides_[channel] = decodeOverride(data, size);
        }
    }
}

bool ChannelSettings::writeOverride(std::uint8_t channel, const Override& value) {
    if (channel >= slots_) return false;
    if (layout_ == Layout::packed) {
        // The group's item with this channel's new value; the others as in RAM.
        const std::size_t first = channel - channel % packedOverrides;
        std::uint8_t data[packedOverrides * overrideBytes]{};
        std::size_t size = 0u;
        for (std::size_t index = 0u; index < packedOverrides && first + index < slots_; ++index) {
            encodeOverride(first + index == channel ? value : overrides_[first + index], data + size);
            size += overrideBytes;
        }
        if (!store(overrideItem(channel)).commit(data, size)) return false;
    } else {
        std::uint8_t data[overrideBytes]{};
        encodeOverride(value, data);
        if (!store(overrideItem(channel)).commit(data, sizeof data)) return false;
    }
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
