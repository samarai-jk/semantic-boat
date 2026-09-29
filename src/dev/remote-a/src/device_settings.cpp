#include "device_settings.hpp"

namespace remote_a {
namespace {

constexpr std::uint8_t settingsVersion = 4u;
constexpr std::size_t payloadSize = 6u;

} // namespace

bool DeviceSettingsStore::load(DeviceSettings& settings) {
    std::uint8_t payload[slstm32::storage::RedundantBlobStore::maxPayloadBytes]{};
    std::size_t size{};
    if (!store_.load(payload, sizeof payload, size) ||
        (size < 3u || size > payloadSize) ||
        (payload[0] < 1u || payload[0] > settingsVersion) ||
        size != static_cast<std::size_t>(payload[0]) + 2u ||
        payload[1] > static_cast<std::uint8_t>(BeepVolume::maximum) ||
        payload[2] > static_cast<std::uint8_t>(LedMode::off) ||
        (payload[0] >= 2u && size >= 4u &&
         payload[3] > static_cast<std::uint8_t>(semantic_display::AlertLevel::emergency)) ||
        (payload[0] >= 3u && (size < 5u || payload[4] > 1u)) ||
        (payload[0] >= 4u && (size < 6u || payload[5] > 1u))) {
        return false;
    }
    settings.beepVolume = static_cast<BeepVolume>(payload[1]);
    settings.ledMode = static_cast<LedMode>(payload[2]);
    if (payload[0] >= 2u && size >= 4u &&
        payload[3] <= static_cast<std::uint8_t>(semantic_display::AlertLevel::emergency)) {
        settings.minimumAlertLevel = static_cast<semantic_display::AlertLevel>(payload[3]);
    } else {
        settings.minimumAlertLevel = semantic_display::AlertLevel::info;
    }
    settings.developerMode = payload[0] >= 3u && size >= 5u && payload[4] != 0u;
    settings.sleeping = payload[0] >= 4u && size >= 6u && payload[5] != 0u;
    return true;
}

bool DeviceSettingsStore::save(const DeviceSettings& settings) {
    const std::uint8_t payload[payloadSize]{
        settingsVersion,
        static_cast<std::uint8_t>(settings.beepVolume),
        static_cast<std::uint8_t>(settings.ledMode),
        static_cast<std::uint8_t>(settings.minimumAlertLevel),
        static_cast<std::uint8_t>(settings.developerMode ? 1u : 0u),
        static_cast<std::uint8_t>(settings.sleeping ? 1u : 0u),
    };
    return store_.commit(payload, sizeof payload);
}

} // namespace remote_a
