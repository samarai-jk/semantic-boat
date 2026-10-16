#include "device_settings.hpp"

namespace lab_test_01 {
namespace {

// Version 1: boot count. Version 2 adds the NMEA 2000 address; version-1
// records load with the default address.
constexpr std::uint8_t recordVersion = 2u;
constexpr std::size_t version1Bytes = 5u;
constexpr std::size_t recordBytes = 6u;

} // namespace

bool DeviceSettingsStore::load(DeviceSettings& settings) {
    settings = {};
    std::uint8_t record[slstm32::storage::RedundantBlobStore::maxPayloadBytes]{};
    std::size_t size = 0u;
    if (!store_.load(record, sizeof record, size)) return false;
    if (size < version1Bytes || record[0] == 0u || record[0] > recordVersion) return false;
    settings.bootCount = static_cast<std::uint32_t>(record[1]) |
                         static_cast<std::uint32_t>(record[2]) << 8u |
                         static_cast<std::uint32_t>(record[3]) << 16u |
                         static_cast<std::uint32_t>(record[4]) << 24u;
    if (record[0] >= 2u && size >= recordBytes) settings.n2kAddress = record[5];
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
    };
    return store_.commit(record, sizeof record);
}

} // namespace lab_test_01
