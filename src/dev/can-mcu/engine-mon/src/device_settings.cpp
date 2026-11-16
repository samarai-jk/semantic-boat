#include "device_settings.hpp"

namespace engine_mon {
namespace {

// Version 1: boot count, NMEA 2000 address. Version 2: and the EEPROM
// layout (a version 1 record belongs to layout 1).
constexpr std::uint8_t recordVersion = 2u;
constexpr std::size_t recordBytes = 7u;
constexpr std::size_t version1Bytes = 6u;

} // namespace

bool DeviceSettingsStore::load(DeviceSettings& settings) {
    settings = {};
    std::uint8_t record[slstm32::storage::RedundantBlobStore::maxPayloadBytes]{};
    std::size_t size = 0u;
    if (!store_.load(record, sizeof record, size)) return false;
    if (size < version1Bytes || record[0] < 1u || record[0] > recordVersion) return false;
    if (record[0] >= 2u && size < recordBytes) return false;
    settings.bootCount = static_cast<std::uint32_t>(record[1]) |
                         static_cast<std::uint32_t>(record[2]) << 8u |
                         static_cast<std::uint32_t>(record[3]) << 16u |
                         static_cast<std::uint32_t>(record[4]) << 24u;
    settings.n2kAddress = record[5];
    settings.layout = record[0] >= 2u ? record[6] : 1u;
    return true;
}

bool DeviceSettingsStore::save(const DeviceSettings& settings) {
    const std::uint8_t record[recordBytes]{
        recordVersion,
        static_cast<std::uint8_t>(settings.bootCount),
        static_cast<std::uint8_t>(settings.bootCount >> 8u),
        static_cast<std::uint8_t>(settings.bootCount >> 16u),
        static_cast<std::uint8_t>(settings.bootCount >> 24u),
        settings.n2kAddress,
        settings.layout,
    };
    return store_.commit(record, sizeof record);
}

} // namespace engine_mon
