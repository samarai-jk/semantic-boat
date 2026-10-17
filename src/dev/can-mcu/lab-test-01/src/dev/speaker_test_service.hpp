// DEVELOPMENT AND TEST CODE: compiled only with SB_DEV_TOOLS (AGENTS.md,
// "Firmware code categories and build profiles"). Never reference this from
// product code.
#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/console.hpp"
#include "slstm32/event_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace lab_test_01 {

// TEMPORARY speaker experiment (DFRobot FIT0449, 8002 amplifier, on PA4):
// the DAC on PA4 plays samples at 16 kHz from a TIM6 interrupt, mixed from
// three oscillators (sine, square, triangle) whose frequency and level the
// main loop changes every millisecond. Buttons: BF1/BF2 volume, BF3 sweep,
// BF4 waveforms, BF5 alarm sounds, BF6 chimes; console "speaker".
// Takes PA4 from the 1-Wire bus while compiled in; remove after the test.
class SpeakerTestService final : public slstm32::Service {
public:
    SpeakerTestService(slstm32::Runtime runtime, slstm32::EventBus& events, slstm32::EventId buttonEvent,
                       slstm32::Logger log, slstm32::CommandConsole& console)
        : runtime_(runtime), events_(events), buttonEvent_(buttonEvent), log_(log), console_(console) {}

    bool init() override;
    void run() override;

private:
    enum class Program : std::uint8_t { none, tone, sweep, waveform, alarm, chime };
    static void onButton(slstm32::EventId, const void* payload, std::uint8_t size, void* context);
    static void command(void* context, std::size_t argc, const char* const* argv);
    void pressed(std::uint8_t id);
    void start(Program program, std::uint8_t variant, std::uint32_t now);
    void stop();
    void update(std::uint32_t elapsed);
    void setVolume(std::uint8_t step);

    slstm32::Runtime runtime_;
    slstm32::EventBus& events_;
    slstm32::EventId buttonEvent_{};
    slstm32::Logger log_;
    slstm32::CommandConsole& console_;
    Program program_{Program::none};
    std::uint8_t variant_{};
    std::uint8_t waveformVariant_{};
    std::uint8_t alarmVariant_{};
    std::uint8_t chimeVariant_{};
    std::uint8_t volumeStep_{4u};
    std::uint32_t startedAt_{};
    std::uint32_t lastUpdate_{};
    std::uint32_t toneHz_{};
    std::uint32_t toneMs_{};
    std::uint8_t toneWave_{};
    std::int32_t sweepStep_{-1};
};

} // namespace lab_test_01
