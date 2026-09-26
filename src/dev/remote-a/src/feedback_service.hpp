#pragma once

#include <cstdint>
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
    void setSleeping(bool active);
    bool sleepCueComplete() const { return !sleepCueActive_; }

private:
    static void eventThunk(slstm32::EventId, const void*, std::uint8_t, void*);
    void onButton();

    slstm32::Runtime runtime_;
    slstm32::EventBus& events_;
    slstm32::drivers::LedDriver& signalLed_;
    slstm32::drivers::RgbLedDriver& statusLed_;
    slstm32::drivers::ToneDriver& buzzer_;
    std::uint32_t startupEndsAt_{};
    std::uint32_t nextWakeCueAt_{};
    std::uint8_t wakeCueBeeps_{};
    bool starting_{};
    bool alert_{};
    bool sleeping_{};
    bool sleepCueActive_{};
    bool wakeCueActive_{};
};

} // namespace remote_a
