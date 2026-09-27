#include "device_settings.hpp"

namespace remote_a {
namespace {

constexpr std::uint8_t settingsVersion = 2u;
constexpr std::size_t payloadSize = 4u;

} // namespace

bool DeviceSettingsStore::load(DeviceSettings& settings) {
    std::uint8_t payload[slstm32::storage::RedundantBlobStore::maxPayloadBytes]{};
    std::size_t size{};
    if (!store_.load(payload, sizeof payload, size) ||
        (size != 3u && size != payloadSize) ||
        (payload[0] != 1u && payload[0] != settingsVersion) ||
        payload[1] > static_cast<std::uint8_t>(BeepVolume::maximum) ||
        payload[2] > static_cast<std::uint8_t>(LedMode::off) ||
        (payload[0] == settingsVersion &&
         payload[3] > static_cast<std::uint8_t>(semantic_display::AlertLevel::emergency))) {
        return false;
    }
    settings.beepVolume = static_cast<BeepVolume>(payload[1]);
    settings.ledMode = static_cast<LedMode>(payload[2]);
    if (payload[0] == settingsVersion && size == payloadSize &&
        payload[3] <= static_cast<std::uint8_t>(semantic_display::AlertLevel::emergency)) {
        settings.minimumAlertLevel = static_cast<semantic_display::AlertLevel>(payload[3]);
    } else {
        settings.minimumAlertLevel = semantic_display::AlertLevel::info;
    }
    return true;
}

bool DeviceSettingsStore::save(const DeviceSettings& settings) {
    const std::uint8_t payload[payloadSize]{
        settingsVersion,
        static_cast<std::uint8_t>(settings.beepVolume),
        static_cast<std::uint8_t>(settings.ledMode),
        static_cast<std::uint8_t>(settings.minimumAlertLevel),
    };
    return store_.commit(payload, sizeof payload);
}

} // namespace remote_a
