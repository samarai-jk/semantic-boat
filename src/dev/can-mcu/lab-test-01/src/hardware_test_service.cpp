#include "hardware_test_service.hpp"
#include "events.hpp"

namespace lab_test_01 {
namespace {

using slstm32::LogLevel;

constexpr std::uint32_t buttonToneHz[]{1000u, 1500u, 2000u, 2700u, 3500u, 4500u};
constexpr std::uint32_t buttonToneMs = 40u;
// Tone level 1.0 is 50 % duty, the loudest setting, reserved for alarms.
// Button and startup cues use 1 % duty.
constexpr float clickLevel = 0.02f;
constexpr std::uint32_t startupToneHz = 2700u;
constexpr std::uint32_t startupToneMs = 60u;
constexpr std::uint32_t redPulseMs = 100u;
constexpr std::uint32_t levelPollPeriodMs = 10u;
constexpr float greenBrightness = 0.3f;

} // namespace

bool HardwareTestService::init() {
    if (!events_.subscribe(lab_test_01::buttonPressed, &onButton, this)) return false;
    greenLed_.setBrightness(greenBrightness);
    greenLed_.on();
    redLed_.off();
    for (std::uint8_t id = 0; id < buttonCount_; ++id) {
        if (buttons_.pressed(id)) buttonLevels_ = static_cast<std::uint16_t>(buttonLevels_ | (1u << id));
    }
    log_.write(LogLevel::info, "hardware test ready: press BF1..BF6");
    buzzer_.play(startupToneHz, startupToneMs, clickLevel);
    return true;
}

void HardwareTestService::run() {
    pollButtonLevels(runtime_.millis());
}

void HardwareTestService::pollButtonLevels(std::uint32_t now) {
    if (static_cast<std::int32_t>(now - nextLevelPollAt_) < 0) return;
    nextLevelPollAt_ = now + levelPollPeriodMs;
    for (std::uint8_t id = 0; id < buttonCount_; ++id) {
        const auto mask = static_cast<std::uint16_t>(1u << id);
        const bool pressed = buttons_.pressed(id);
        if (pressed == ((buttonLevels_ & mask) != 0u)) continue;
        buttonLevels_ = static_cast<std::uint16_t>(pressed ? buttonLevels_ | mask : buttonLevels_ & ~mask);
        log_.writef(LogLevel::debug, "BF%u level %s (polled)", static_cast<unsigned>(id + 1u),
                    pressed ? "low" : "high");
    }
}

void HardwareTestService::onButton(slstm32::EventId, const void* payload,
                                   std::uint8_t size, void* context) {
    if (!context || !payload || size != sizeof(slstm32::drivers::ButtonEvent)) return;
    const auto& event = *static_cast<const slstm32::drivers::ButtonEvent*>(payload);
    if (event.pressed) static_cast<HardwareTestService*>(context)->onButtonPressed(event.id);
}

void HardwareTestService::onButtonPressed(std::uint8_t id) {
    if (id >= sizeof buttonToneHz / sizeof buttonToneHz[0]) return;
    const auto frequency = buttonToneHz[id];
    log_.writef(LogLevel::info, "BF%u pressed, click %lu Hz", static_cast<unsigned>(id + 1u),
                static_cast<unsigned long>(frequency));
    redLed_.pulse(redPulseMs);
    buzzer_.play(frequency, buttonToneMs, clickLevel);
}

} // namespace lab_test_01
