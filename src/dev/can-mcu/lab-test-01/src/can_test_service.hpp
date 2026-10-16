#pragma once

#include <cstdint>
#include "slstm32/drivers/can_bus.hpp"
#include "slstm32/drivers/led.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace lab_test_01 {

// Raw CAN bring-up, before NMEA 2000: sends a numbered test frame every
// second (extended ID 0x18FF0042, data = counter LE32, uptime s LE16,
// 0xCA 0xFE), logs received frames (at most ten per second, the rest are
// counted), logs every change of the controller state, flashes the yellow
// RJ45 LED on traffic, and logs a summary every 10 s.
class CanTestService final : public slstm32::Service {
public:
    CanTestService(slstm32::Runtime runtime, slstm32::Logger log,
                   slstm32::drivers::CanBusDriver& can, slstm32::drivers::LedDriver& activityLed)
        : runtime_(runtime), log_(log), can_(can), activityLed_(activityLed) {}

    bool init() override;
    void run() override;

private:
    void sendTestFrame(std::uint32_t now);
    void drainReceived(std::uint32_t now);
    void watchStatus();
    void showActivity();
    void summary(std::uint32_t now);
    void logFrame(const slstm32::drivers::CanFrame& frame) const;

    slstm32::Runtime runtime_;
    slstm32::Logger log_;
    slstm32::drivers::CanBusDriver& can_;
    slstm32::drivers::LedDriver& activityLed_;
    std::uint32_t nextSendAt_{};
    std::uint32_t nextSummaryAt_{};
    std::uint32_t logWindowStartedAt_{};
    std::uint32_t sequence_{};
    std::uint32_t lastTransmitted_{};
    std::uint32_t suppressedFrames_{};
    std::uint8_t loggedInWindow_{};
    slstm32::drivers::CanStatus lastStatus_{};
};

} // namespace lab_test_01
