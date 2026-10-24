#pragma once

#include <cstdint>
#include "n2k/can_id.hpp"
#include "n2k/node.hpp"
#include "slstm32/drivers/can_bus.hpp"
#include "slstm32/drivers/led.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace can_mcu {

// CAN and NMEA 2000 visibility for bring-up: logs every change of the
// controller state, flashes the yellow RJ45 LED on traffic, logs messages
// the N2K node passes to the application (at most ten per second), and logs
// a CAN/N2K summary every 10 s.
class CanMonitorService final : public slstm32::Service {
public:
    CanMonitorService(slstm32::Runtime runtime, slstm32::Logger log,
                      slstm32::drivers::CanBusDriver& can, n2k::Node& node,
                      slstm32::drivers::LedDriver& activityLed)
        : runtime_(runtime), log_(log), can_(can), node_(node), activityLed_(activityLed) {}

    bool init() override;
    void run() override;

    // Blinks the activity LED fast for this long instead of showing traffic
    // (identify); 0 ends it.
    void identify(std::uint32_t durationMs);
    // n2k::MessageHandler
    static void onMessage(void* context, const n2k::CanId& id, const std::uint8_t* data,
                          std::uint8_t length);

private:
    void logMessage(const n2k::CanId& id, const std::uint8_t* data, std::uint8_t length);
    void watchStatus();
    void showActivity();
    void summary(std::uint32_t now);

    slstm32::Runtime runtime_;
    slstm32::Logger log_;
    slstm32::drivers::CanBusDriver& can_;
    n2k::Node& node_;
    slstm32::drivers::LedDriver& activityLed_;
    std::uint32_t nextSummaryAt_{};
    std::uint32_t logWindowStartedAt_{};
    std::uint32_t lastTraffic_{};
    std::uint32_t identifyUntil_{};
    bool identifying_{};
    std::uint32_t suppressedMessages_{};
    std::uint8_t loggedInWindow_{};
    slstm32::drivers::CanStatus lastStatus_{};
};

} // namespace can_mcu
