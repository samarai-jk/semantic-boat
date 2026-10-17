#pragma once

// DEVELOPMENT AND TEST CODE: compiled only with SB_DEV_TOOLS (AGENTS.md,
// "Firmware code categories and build profiles"). Never reference this from
// product code.

#include <cstdint>
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/pwm_led.hpp"
#include "slstm32/drivers/tone.hpp"
#include "slstm32/event_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace lab_test_01 {

// Bring-up checks for the LT1 user interface: green LED shows "running",
// every button logs its press, flashes the red LED, and plays a quiet click
// (one frequency per button, so buttons can be told apart by ear). Button
// levels are also polled and logged on change, which separates wiring
// faults from interrupt problems.
class HardwareTestService final : public slstm32::Service {
public:
    HardwareTestService(slstm32::Runtime runtime, slstm32::EventBus& events,
                        slstm32::Logger log, slstm32::drivers::ButtonDriver& buttons,
                        std::uint8_t buttonCount, slstm32::drivers::PwmLedDriver& greenLed,
                        slstm32::drivers::PwmLedDriver& redLed,
                        slstm32::drivers::ToneDriver& buzzer)
        : runtime_(runtime), events_(events), log_(log), buttons_(buttons),
          buttonCount_(buttonCount), greenLed_(greenLed), redLed_(redLed), buzzer_(buzzer) {}

    bool init() override;
    void run() override;

private:
    static void onButton(slstm32::EventId, const void* payload, std::uint8_t size,
                         void* context);
    void onButtonPressed(std::uint8_t id);
    void pollButtonLevels(std::uint32_t now);

    slstm32::Runtime runtime_;
    slstm32::EventBus& events_;
    slstm32::Logger log_;
    slstm32::drivers::ButtonDriver& buttons_;
    std::uint8_t buttonCount_{};
    slstm32::drivers::PwmLedDriver& greenLed_;
    slstm32::drivers::PwmLedDriver& redLed_;
    slstm32::drivers::ToneDriver& buzzer_;
    std::uint16_t buttonLevels_{};
    std::uint32_t nextLevelPollAt_{};
};

} // namespace lab_test_01
