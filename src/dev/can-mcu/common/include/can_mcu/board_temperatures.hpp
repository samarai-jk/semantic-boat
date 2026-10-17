#pragma once

#include <cstdint>
#include "can_mcu/mcu_monitor.hpp"
#include "slstm32/drivers/tmp75.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"
#include "slstm32/service.hpp"

namespace can_mcu {

// The two temperature probes every can-mcu has: the TMP75 on the board (case
// temperature) and the MCU die sensor. Feeds both into measurement channels
// with fixed identities, so their settings follow the board.
class BoardTemperatures final : public slstm32::Service {
public:
    static constexpr auto boardIdentity = slstm32::sensors::Identity::internal(0u);
    static constexpr auto mcuIdentity = slstm32::sensors::Identity::internal(1u);

    BoardTemperatures(slstm32::Runtime runtime, slstm32::drivers::Tmp75Driver& tmp75, McuMonitor& mcu,
                      slstm32::sensors::Measurement& board, slstm32::sensors::Measurement& die,
                      std::uint32_t periodMs = 1000u)
        : runtime_(runtime), tmp75_(tmp75), mcu_(mcu), board_(board), die_(die), periodMs_(periodMs) {}

    bool init() override;
    void run() override;

private:
    slstm32::Runtime runtime_;
    slstm32::drivers::Tmp75Driver& tmp75_;
    McuMonitor& mcu_;
    slstm32::sensors::Measurement& board_;
    slstm32::sensors::Measurement& die_;
    std::uint32_t periodMs_{};
    std::uint32_t nextAt_{};
};

} // namespace can_mcu
