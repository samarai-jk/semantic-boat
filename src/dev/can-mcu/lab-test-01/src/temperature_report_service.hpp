#pragma once

#include <cstdint>
#include "can_mcu/mcu_monitor.hpp"
#include "n2k/node.hpp"
#include "slstm32/drivers/tmp75.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace lab_test_01 {

// Publishes the board temperature (TMP75, instance 0) and the MCU die
// temperature (instance 1) as PGN 130316 every 2 s once the node has an
// address. Both use source "inside".
class TemperatureReportService final : public slstm32::Service {
public:
    TemperatureReportService(slstm32::Runtime runtime, n2k::Node& node,
                             slstm32::drivers::Tmp75Driver& board, can_mcu::McuMonitor& mcu)
        : runtime_(runtime), node_(node), board_(board), mcu_(mcu) {}

    bool init() override;
    void run() override;

private:
    void send(std::uint8_t instance, std::int32_t milliCelsius);

    slstm32::Runtime runtime_;
    n2k::Node& node_;
    slstm32::drivers::Tmp75Driver& board_;
    can_mcu::McuMonitor& mcu_;
    std::uint32_t nextReportAt_{};
};

} // namespace lab_test_01
