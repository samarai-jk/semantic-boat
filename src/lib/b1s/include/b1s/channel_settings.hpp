#pragma once

#include <cstddef>
#include <cstdint>
#include "b1s/messages.hpp"
#include "slstm32/storage/byte_storage.hpp"
#include "slstm32/storage/redundant_blob_store.hpp"

namespace b1s {

// B1S configuration of a device set over CAN (CONFIGURATION.md), stored in
// EEPROM on top of the compiled-in defaults: the device role and, per B1S
// channel number below the slot count, location, label, encoding, standard
// PGN instance and source, send periods, and sending on change. Each item is
// its own RedundantBlobStore (128 bytes of EEPROM), so a write never risks
// another item. RAM: an Override (8 bytes) per slot, needed at every send;
// the texts are read from EEPROM when needed. (Alarm rules and the output
// function belong to the slstm32 MeasurementRegistry.)
//
// Layouts: perChannel stores the overrides of each channel in an item of
// their own (3 items per channel); packed stores those of five channels in
// one item (2.2 items per channel), for devices with many channels and
// little EEPROM. The layout is the device's choice and part of its EEPROM
// layout: changing it means erasing the area.
class ChannelSettings {
public:
    enum class Text : std::uint8_t { role, location, label };
    enum class Layout : std::uint8_t { perChannel, packed };
    static constexpr std::size_t packedOverrides = 5u; // 8 bytes each, a full item

    // Stored overrides of a channel; notSet = use the compiled-in default.
    static constexpr std::uint8_t notSet = 0xFFu;
    static constexpr std::uint16_t periodNotSet = 0xFFFFu;
    struct Override {
        std::uint8_t publish{notSet};          // Publish value
        std::uint8_t standardInstance{notSet}; // instance of the standard PGN
        std::uint8_t standardSource{notSet};   // source field of the standard PGN
        std::uint8_t onChange{notSet};         // 0 or 1: also sent at once on a change
        std::uint16_t period{periodNotSet};    // 0.1 s
        std::uint16_t rawPeriod{periodNotSet}; // 0.1 s, 0 = never
    };

    template <std::size_t Slots>
    ChannelSettings(slstm32::storage::ByteStorage& storage, std::uint32_t offset, Override (&overrides)[Slots],
                    Layout layout = Layout::perChannel)
        : storage_(storage), offset_(offset), overrides_(overrides), slots_(Slots), layout_(layout) {}

    static constexpr std::size_t requiredBytes(std::size_t slots, Layout layout = Layout::perChannel) {
        return (1u + (layout == Layout::packed ? 2u * slots + (slots + packedOverrides - 1u) / packedOverrides
                                               : 3u * slots)) *
               slstm32::storage::RedundantBlobStore::requiredBytes;
    }

    // Reads the overrides of all slots. Missing records keep the defaults.
    void load();
    std::size_t slots() const { return slots_; }
    const Override* overrideOf(std::uint8_t channel) const { return channel < slots_ ? &overrides_[channel] : nullptr; }
    bool writeOverride(std::uint8_t channel, const Override& value);

    // Text items (the role ignores the channel). readText returns false if
    // nothing is stored; out is then empty.
    bool readText(Text text, std::uint8_t channel, char (&out)[maxTextLength + 1u]);
    bool writeText(Text text, std::uint8_t channel, const char* value);

private:
    slstm32::storage::RedundantBlobStore store(std::size_t item);
    std::size_t itemOf(Text text, std::uint8_t channel) const;
    std::size_t overrideItem(std::uint8_t channel) const;

    slstm32::storage::ByteStorage& storage_;
    std::uint32_t offset_{};
    Override* overrides_{};
    std::size_t slots_{};
    Layout layout_{};
};

} // namespace b1s
