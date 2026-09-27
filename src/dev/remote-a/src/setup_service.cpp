#include "setup_service.hpp"
#include "hal_eeprom.hpp"

namespace remote_a {
namespace {

constexpr std::string_view mainItems[]{"SETTINGS", "APPS"};
constexpr std::string_view settingItems[]{"BEEP VOLUME", "LED MODE", "SHOW ALARM LEVELS"};
constexpr std::string_view beepItems[]{"OFF", "MIN", "MEDIUM", "MAX"};
constexpr std::string_view ledItems[]{"NORMAL", "SUBDUED", "OFF"};
constexpr std::string_view alertLevelItems[]{"INFO +", "WARNING +", "ALARM +", "EMERGENCY ONLY"};

template <std::size_t Size>
std::string_view itemAt(const std::string_view (&items)[Size], std::size_t index) {
    return index < Size ? items[index] : std::string_view{};
}

} // namespace

bool SetupService::init() {
    DeviceSettings loaded{};
    if (storageAvailable_ && store_.load(loaded)) settings_ = loaded;
    apply();
    return true;
}

void SetupService::open() {
    (void)display_.showSelection("SETUP",
        {this, 2u, &mainItem, &mainCompleted});
}

void SetupService::openSettings() {
    (void)display_.showSelection("SETTINGS",
        {this, 3u, &settingsItem, &settingsCompleted});
}

void SetupService::openBeepVolume() {
    (void)display_.showSelection("BEEP VOLUME",
        {this, 4u, &beepItem, &beepCompleted},
        static_cast<std::size_t>(settings_.beepVolume));
}

void SetupService::openLedMode() {
    (void)display_.showSelection("LED MODE",
        {this, 3u, &ledItem, &ledCompleted},
        static_cast<std::size_t>(settings_.ledMode));
}

void SetupService::openAlertLevel() {
    (void)display_.showSelection("SHOW ALARM LEVELS",
        {this, 4u, &alertLevelItem, &alertLevelCompleted},
        static_cast<std::size_t>(settings_.minimumAlertLevel));
}

std::string_view SetupService::mainItem(void*, std::size_t index) {
    return itemAt(mainItems, index);
}

std::string_view SetupService::settingsItem(void*, std::size_t index) {
    return itemAt(settingItems, index);
}

std::string_view SetupService::beepItem(void*, std::size_t index) {
    return itemAt(beepItems, index);
}

std::string_view SetupService::ledItem(void*, std::size_t index) {
    return itemAt(ledItems, index);
}

std::string_view SetupService::alertLevelItem(void*, std::size_t index) {
    return itemAt(alertLevelItems, index);
}

void SetupService::mainCompleted(void* context, semantic_display::SelectionResult result,
                                 std::size_t index) {
    auto& self = *static_cast<SetupService*>(context);
    if (result == semantic_display::SelectionResult::cancelled) return;
    if (index == 0u) self.openSettings();
    else if (index == 1u) self.configurations_.requestList();
}

void SetupService::settingsCompleted(void* context,
                                     semantic_display::SelectionResult result,
                                     std::size_t index) {
    auto& self = *static_cast<SetupService*>(context);
    if (result == semantic_display::SelectionResult::cancelled) {
        self.open();
    } else if (index == 0u) {
        self.openBeepVolume();
    } else if (index == 1u) {
        self.openLedMode();
    } else if (index == 2u) {
        self.openAlertLevel();
    }
}

void SetupService::alertLevelCompleted(void* context,
                                       semantic_display::SelectionResult result,
                                       std::size_t index) {
    auto& self = *static_cast<SetupService*>(context);
    if (result == semantic_display::SelectionResult::cancelled) {
        self.openSettings();
        return;
    }
    if (index >= 4u) return;
    const auto selected = static_cast<semantic_display::AlertLevel>(index);
    if (self.settings_.minimumAlertLevel == selected) {
        self.openSettings();
        return;
    }
    self.settings_.minimumAlertLevel = selected;
    self.apply();
    if (self.persist()) self.openSettings();
}

void SetupService::beepCompleted(void* context, semantic_display::SelectionResult result,
                                 std::size_t index) {
    auto& self = *static_cast<SetupService*>(context);
    if (result == semantic_display::SelectionResult::cancelled) {
        self.openSettings();
        return;
    }
    if (index >= 4u) return;
    const auto selected = static_cast<BeepVolume>(index);
    if (self.settings_.beepVolume == selected) {
        self.openSettings();
        return;
    }
    self.settings_.beepVolume = selected;
    self.apply();
    if (self.persist()) self.openSettings();
}

void SetupService::ledCompleted(void* context, semantic_display::SelectionResult result,
                                std::size_t index) {
    auto& self = *static_cast<SetupService*>(context);
    if (result == semantic_display::SelectionResult::cancelled) {
        self.openSettings();
        return;
    }
    if (index >= 3u) return;
    const auto selected = static_cast<LedMode>(index);
    if (self.settings_.ledMode == selected) {
        self.openSettings();
        return;
    }
    self.settings_.ledMode = selected;
    self.apply();
    if (self.persist()) self.openSettings();
}

void SetupService::apply() {
    feedback_.setSettings(settings_);
    transport_.setActivityIndicatorEnabled(settings_.ledMode == LedMode::normal);
    alarms_.setMinimumLevel(settings_.minimumAlertLevel);
}

bool SetupService::persist() {
    clearEepromIoError();
    if (storageAvailable_ && store_.save(settings_)) return true;
    display_.showModal("STORAGE ERROR", eepromIoErrorMessage(),
                       semantic_display::ModalSeverity::error);
    return false;
}

} // namespace remote_a
