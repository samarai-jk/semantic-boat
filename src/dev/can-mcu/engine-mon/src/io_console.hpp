#pragma once

#include <cstddef>
#include <cstdint>
#include "board.hpp"
#include "can_mcu/mcu_monitor.hpp"
#include "slstm32/console.hpp"
#include "slstm32/drivers/digital_input.hpp"
#include "slstm32/drivers/pwm_output.hpp"
#include "slstm32/log.hpp"

namespace engine_mon {

// Service command "io": the state of every host input and output (digital
// input levels and their change counts, analog inputs in mV, output duty).
// Read-only; switching outputs by hand is a development tool.
class IoConsole {
public:
    IoConsole(slstm32::Logger log, slstm32::drivers::DigitalInputDriver* const* inputs,
              slstm32::drivers::PwmOutputDriver* const* outputs, can_mcu::McuMonitor& mcu,
              const std::size_t* analogInputs)
        : log_(log), inputs_(inputs), outputs_(outputs), mcu_(mcu), analogInputs_(analogInputs) {}

    bool registerCommands(slstm32::CommandConsole& console);
    void show() const;

private:
    static void ioCommand(void* context, std::size_t argc, const char* const* argv);

    slstm32::Logger log_;
    slstm32::drivers::DigitalInputDriver* const* inputs_;
    slstm32::drivers::PwmOutputDriver* const* outputs_;
    can_mcu::McuMonitor& mcu_;
    const std::size_t* analogInputs_; // McuMonitor input index of AI0..AI2
};

} // namespace engine_mon
