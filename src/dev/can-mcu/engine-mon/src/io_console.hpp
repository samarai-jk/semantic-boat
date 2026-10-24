#pragma once

#include <cstddef>
#include <cstdint>
#include "board.hpp"
#include "can_mcu/mcu_monitor.hpp"
#include "semantic_modules/ad_input_iso_12v.hpp"
#include "slstm32/console.hpp"
#include "slstm32/drivers/pwm_output.hpp"
#include "slstm32/log.hpp"

namespace engine_mon {

// Service command "io": the state of every host input and output: the
// digital inputs of AD1/AD2 (12 V present or off, with their change counts),
// their analog inputs in V at the terminal, the MCU analog inputs in mV, and
// the output duty. Read-only; switching outputs by hand is a development tool.
class IoConsole {
public:
    // modules: AD1, AD2.
    IoConsole(slstm32::Logger log, semantic_modules::AdInputIso12v* const* modules,
              slstm32::drivers::PwmOutputDriver* const* outputs, can_mcu::McuMonitor& mcu,
              const std::size_t* analogInputs)
        : log_(log), modules_(modules), outputs_(outputs), mcu_(mcu), analogInputs_(analogInputs) {}

    bool registerCommands(slstm32::CommandConsole& console);
    void show() const;

private:
    static void ioCommand(void* context, std::size_t argc, const char* const* argv);

    slstm32::Logger log_;
    semantic_modules::AdInputIso12v* const* modules_;
    slstm32::drivers::PwmOutputDriver* const* outputs_;
    can_mcu::McuMonitor& mcu_;
    const std::size_t* analogInputs_; // McuMonitor input index of AI0..AI2
};

} // namespace engine_mon
