#pragma once

// DEVELOPMENT AND TEST CODE: compiled only with SB_DEV_TOOLS (AGENTS.md,
// "Firmware code categories and build profiles"). Never reference this from
// product code.

#include <cstdint>
#include "health_service.hpp"
#include "n2k/node.hpp"
#include "semantic_modules/ad_input_iso_12v.hpp"
#include "slstm32/drivers/pwm_led.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace lab_test_01 {

// Realistic background work for unattended runs, and a visible trap:
// - green LED blinks briefly every 2 s (alive);
// - red LED blinks once the health record has seen an issue (latched until
//   reset), so a problem during the night is visible in the morning;
// - the input module's analog channels go out as PGN 127508 (battery status,
//   instances 0..3) and its digital inputs as PGN 127501 (binary switch bank
//   status, bank 0) once per second.
// The buzzer is left alone.
class ActivityService final : public slstm32::Service {
public:
    ActivityService(slstm32::Runtime runtime, n2k::Node& node, semantic_modules::AdInputIso12v& module,
                    HealthService& health, slstm32::drivers::PwmLedDriver& greenLed,
                    slstm32::drivers::PwmLedDriver& redLed)
        : runtime_(runtime), node_(node), module_(module), health_(health), greenLed_(greenLed),
          redLed_(redLed) {}

    bool init() override;
    void run() override;

private:
    void publish();

    slstm32::Runtime runtime_;
    n2k::Node& node_;
    semantic_modules::AdInputIso12v& module_;
    HealthService& health_;
    slstm32::drivers::PwmLedDriver& greenLed_;
    slstm32::drivers::PwmLedDriver& redLed_;
    std::uint32_t nextPublishAt_{};
    bool alarmShown_{};
};

} // namespace lab_test_01
