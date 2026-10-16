#pragma once

#include <cstdint>
#include "can_mcu/i2c_bus.hpp"
#include "can_mcu/mcu_monitor.hpp"
#include "slstm32/drivers/tmp75.hpp"
#include "slstm32/drivers/tone.hpp"
#include "slstm32/event_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace lab_test_01 {

// Board health for bring-up: logs every supply dip as it happens (checked in
// 100 ms windows) and a periodic heartbeat with uptime, supply range, board
// and MCU temperature, I2C bus faults, and dropped events.
class DiagnosticsService final : public slstm32::Service {
public:
    DiagnosticsService(slstm32::Runtime runtime, slstm32::Logger log, slstm32::EventBus& events,
                       can_mcu::McuMonitor& mcu, slstm32::drivers::Tmp75Driver& boardTemperature,
                       can_mcu::HalI2cBus& i2c, slstm32::drivers::ToneDriver& buzzer)
        : runtime_(runtime), log_(log), events_(events), mcu_(mcu),
          boardTemperature_(boardTemperature), i2c_(i2c), buzzer_(buzzer) {}

    bool init() override;
    void run() override;

private:
    void checkSupply(std::uint32_t now);
    void heartbeat(std::uint32_t now);

    slstm32::Runtime runtime_;
    slstm32::Logger log_;
    slstm32::EventBus& events_;
    can_mcu::McuMonitor& mcu_;
    slstm32::drivers::Tmp75Driver& boardTemperature_;
    can_mcu::HalI2cBus& i2c_;
    slstm32::drivers::ToneDriver& buzzer_;
    std::uint32_t nextSupplyCheckAt_{};
    std::uint32_t nextHeartbeatAt_{};
    std::uint16_t minimumMv_{0xffffu};
    std::uint16_t maximumMv_{};
};

} // namespace lab_test_01
