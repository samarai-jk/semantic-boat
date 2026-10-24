#pragma once

#include <cstddef>
#include "slstm32/console.hpp"
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/pwm_led.hpp"
#include "slstm32/drivers/pwm_output.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

// Seam between product code and development/test code (AGENTS.md, "Firmware
// code categories and build profiles"). The composition root always binds
// devTools(); the build decides what it is:
// - with development tools: src/dev/dev_tools.cpp adds the bring-up command
//   "do" (switch the outputs by hand) and, temporarily, the LEDs on the
//   buttons (red while BF5 is held, green while BF6 is held);
// - without: src/dev_tools_disabled.cpp returns a service that does nothing.
// Product code must not depend on anything inside src/dev/.
namespace engine_mon {

// Everything the development tools may use, filled in by the composition root.
struct DevToolsContext {
    slstm32::Runtime runtime;
    slstm32::Logger log;
    slstm32::CommandConsole& console;
    slstm32::drivers::PwmOutputDriver* const* outputs;
    std::size_t outputCount;
    slstm32::drivers::ButtonDriver& buttons;
    slstm32::drivers::PwmLedDriver& greenLed;
    slstm32::drivers::PwmLedDriver& redLed;
};

slstm32::Service& devTools(const DevToolsContext& context);

} // namespace engine_mon
