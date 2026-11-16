#pragma once

#include <cstddef>
#include <cstdint>
#include "b1s/channel_settings.hpp"
#include "b1s/registry.hpp"
#include "slstm32/sensors/registry.hpp"
#include "slstm32/storage/byte_storage.hpp"
#include "slstm32/storage/keyed_record_store.hpp"
#include "slstm32/storage/redundant_blob_store.hpp"

namespace engine_mon {

// EEPROM layout (AT24C256 on the can-mcu, 32 KiB), from the top down. The
// first four areas sit where lab-test-01 has them; the B1S and channel areas
// are larger (29 channels, B1S channel numbers 0..28, the B1S overrides
// packed five to an item). 31 KiB in all: 896 bytes are left at the bottom.
// Layout 2 (2026-11-16) grew them from 16 to 24 channels, layout 3 (the same
// day) to 29 with the digital inputs; records of an older layout below the
// sensor settings are erased once (DeviceSettings::layout).
// The last 256 bytes: device settings (the redundant store uses 128).
inline constexpr std::uint32_t settingsOffset = 32u * 1024u - 256u;
inline constexpr std::size_t settingsBytes = 256u;
// Calibration of up to four 12v-ad-input-iso modules (one 128-byte slot per
// ADS1115 address 0x48..0x4B).
inline constexpr std::uint32_t moduleCalibrationOffset = settingsOffset - 512u;
// Health record (128 bytes).
inline constexpr std::uint32_t healthRecordOffset = moduleCalibrationOffset - 128u;
// Calibration of the measurement sensors, one 128-byte slot per sensor
// identity.
inline constexpr std::size_t sensorSettingsSlots = 32u;
inline constexpr std::uint32_t sensorSettingsOffset =
    healthRecordOffset - slstm32::storage::KeyedRecordStore::requiredBytes(sensorSettingsSlots);
// B1S configuration set over CAN (device role; location, label, encoding,
// periods, sending on change of B1S channels 0..28).
inline constexpr std::size_t b1sChannelSlots = 29u;
inline constexpr auto b1sSettingsLayout = b1s::ChannelSettings::Layout::packed;
inline constexpr std::uint32_t b1sSettingsOffset =
    sensorSettingsOffset - b1s::ChannelSettings::requiredBytes(b1sChannelSlots, b1sSettingsLayout);
// Alarm rules, fault alarm, and output function of the measurement channels
// (five 128-byte records per channel).
inline constexpr std::size_t measurementChannels = 29u;
inline constexpr std::uint32_t channelProcessingOffset =
    b1sSettingsOffset - slstm32::sensors::MeasurementRegistry::processingBytes(measurementChannels);
static_assert(channelProcessingOffset < 32u * 1024u, "EEPROM layout fits");
// The current layout; settings of an older one are erased at startup.
inline constexpr std::uint8_t eepromLayout = 3u;

struct DeviceSettings {
    std::uint32_t bootCount{};
    // Last claimed NMEA 2000 source address, tried first at the next start.
    std::uint8_t n2kAddress{b1s::product::engineMon.preferredAddress};
    // EEPROM layout the stored channel settings belong to (1 before 2026-11-16).
    std::uint8_t layout{eepromLayout};
};

class DeviceSettingsStore {
public:
    explicit DeviceSettingsStore(slstm32::storage::ByteStorage& storage)
        : store_(storage, settingsOffset, settingsBytes) {}

    // Returns false and leaves `settings` at defaults when no valid record exists.
    bool load(DeviceSettings& settings);
    bool save(const DeviceSettings& settings);

private:
    slstm32::storage::RedundantBlobStore store_;
};

} // namespace engine_mon
