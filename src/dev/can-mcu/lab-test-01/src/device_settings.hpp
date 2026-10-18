#pragma once

#include <cstddef>
#include <cstdint>
#include "b1s/registry.hpp"
#include "slstm32/storage/byte_storage.hpp"
#include "slstm32/storage/keyed_record_store.hpp"
#include "slstm32/storage/redundant_blob_store.hpp"

namespace lab_test_01 {

// EEPROM layout (AT24C256, 32 KiB): the last 256 bytes are reserved for
// device settings, of which the redundant store uses the first 128 bytes.
inline constexpr std::uint32_t settingsOffset = 32u * 1024u - 256u;
inline constexpr std::size_t settingsBytes = 256u;
// Calibration of up to four 12v-ad-input-iso modules (one 128-byte slot per
// ADS1115 address 0x48..0x4B) directly below the settings.
inline constexpr std::uint32_t moduleCalibrationOffset = settingsOffset - 512u;
// Health record for unattended runs (128 bytes) below the module calibration.
inline constexpr std::uint32_t healthRecordOffset = moduleCalibrationOffset - 128u;
// Settings of the measurement channels (calibration, changed alarm
// thresholds), one 128-byte slot per sensor identity, below the health
// record. Generous on purpose: sensors come and go on the bench, and each
// keeps its slot, so no sensor loses its calibration when others are added.
inline constexpr std::size_t sensorSettingsSlots = 32u;
inline constexpr std::uint32_t sensorSettingsOffset =
    healthRecordOffset - slstm32::storage::KeyedRecordStore::requiredBytes(sensorSettingsSlots);

struct DeviceSettings {
    std::uint32_t bootCount{};
    // Last claimed NMEA 2000 source address, tried first at the next start.
    std::uint8_t n2kAddress{b1s::product::labTest01.preferredAddress};
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

} // namespace lab_test_01
