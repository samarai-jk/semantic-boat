#pragma once

#include <cstddef>
#include <string_view>
#include "alarm_service.hpp"
#include "configuration_service.hpp"
#include "device_settings.hpp"
#include "feedback_service.hpp"
#include "semantic_display/display.hpp"
#include "slstm32/service.hpp"
#include "uart_transport.hpp"

namespace remote_a {

struct SetupActions {
    void* context{};
    void (*reset)(void* context){};
    void (*developerModeChanged)(void* context, bool enabled){};
};

class SetupService final : public slstm32::Service {
public:
    SetupService(semantic_display::DisplayService& display,
                 FeedbackService& feedback, UartTransport& transport,
                 ConfigurationService& configurations,
                 AlarmService& alarms, DeviceSettingsStore& store,
                 SetupActions actions = {})
        : display_(display), feedback_(feedback), transport_(transport),
          configurations_(configurations), alarms_(alarms), store_(store),
          actions_(actions) {}

    bool init() override;
    void run() override {}
    void open();
    void setStorageAvailable(bool available) { storageAvailable_ = available; }
    bool setSleeping(bool sleeping);
    const DeviceSettings& settings() const { return settings_; }

private:
    static std::string_view mainItem(void*, std::size_t index);
    static std::string_view settingsItem(void*, std::size_t index);
    static std::string_view beepItem(void*, std::size_t index);
    static std::string_view ledItem(void*, std::size_t index);
    static std::string_view alertLevelItem(void*, std::size_t index);
    static std::string_view developerModeItem(void*, std::size_t index);
    static void mainCompleted(void*, semantic_display::SelectionResult, std::size_t index);
    static void settingsCompleted(void*, semantic_display::SelectionResult, std::size_t index);
    static void beepCompleted(void*, semantic_display::SelectionResult, std::size_t index);
    static void ledCompleted(void*, semantic_display::SelectionResult, std::size_t index);
    static void alertLevelCompleted(void*, semantic_display::SelectionResult,
                                    std::size_t index);
    static void developerModeCompleted(void*, semantic_display::SelectionResult,
                                       std::size_t index);

    void openSettings();
    void openBeepVolume();
    void openLedMode();
    void openAlertLevel();
    void openDeveloperMode();
    void apply();
    bool persist();

    semantic_display::DisplayService& display_;
    FeedbackService& feedback_;
    UartTransport& transport_;
    ConfigurationService& configurations_;
    AlarmService& alarms_;
    DeviceSettingsStore& store_;
    SetupActions actions_{};
    DeviceSettings settings_{};
    bool storageAvailable_{};
};

} // namespace remote_a
