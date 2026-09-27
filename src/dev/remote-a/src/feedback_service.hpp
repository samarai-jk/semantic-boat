#pragma once

#include <cstdint>
#include "device_settings.hpp"
#include "semantic_display/alert.hpp"
#include "slstm32/drivers/led.hpp"
#include "slstm32/drivers/rgb_led.hpp"
#include "slstm32/drivers/tone.hpp"
#include "slstm32/event_bus.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace remote_a {

class FeedbackService final : public slstm32::Service {
public:
    FeedbackService(slstm32::Runtime runtime, slstm32::EventBus& events,
                    slstm32::drivers::LedDriver& signalLed,
                    slstm32::drivers::RgbLedDriver& statusLed,
                    slstm32::drivers::ToneDriver& buzzer)
        : runtime_(runtime), events_(events), signalLed_(signalLed),
          statusLed_(statusLed), buzzer_(buzzer) {}

    bool init() override;
    void run() override;
    void setAlert(bool active);
    void playAttentionCue();
    void playWarningCue();
    void setSystemAlert(semantic_display::AlertLevel level, bool active,
                        bool audible = false);
    void setSystemAlertAudible(bool audible);
    void setOperational();
    void setSleeping(bool active);
    void setSettings(DeviceSettings settings);
    bool sleepCueComplete() const { return !sleepCueActive_; }

private:
    static void eventThunk(slstm32::EventId, const void*, std::uint8_t, void*);
    void onButton();
    float beepLevel() const;
    float statusLedDuty() const;
    void updateStatusLed();
    void startAttentionCue(float level);
    void updateSystemAlarmTone(std::uint32_t now);

    slstm32::Runtime runtime_;
    slstm32::EventBus& events_;
    slstm32::drivers::LedDriver& signalLed_;
    slstm32::drivers::RgbLedDriver& statusLed_;
    slstm32::drivers::ToneDriver& buzzer_;
    std::uint32_t nextWakeCueAt_{};
    std::uint32_t nextAttentionCueAt_{};
    std::uint32_t nextSystemToneAt_{};
    std::uint8_t wakeCueBeeps_{};
    std::uint8_t attentionCueBeeps_{};
    float attentionCueLevel_{};
    DeviceSettings settings_{};
    semantic_display::AlertLevel systemAlertLevel_{semantic_display::AlertLevel::info};
    bool starting_{};
    bool alert_{};
    bool sleeping_{};
    bool sleepCueActive_{};
    bool wakeCueActive_{};
    bool attentionCueActive_{};
    bool initialized_{};
    bool operational_{};
    bool systemAlertActive_{};
    bool systemAlertAudible_{};
    bool emergencyHighTone_{true};
};

} // namespace remote_a
