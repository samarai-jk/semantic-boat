#pragma once

#include <cstdint>
#include "can_mcu/health_service.hpp"
#include "can_mcu/i2c_scan_service.hpp"
#include "n2k/node.hpp"
#include "semantic_modules/ad_input_iso_12v.hpp"
#include "slstm32/console.hpp"
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/drivers/pwm_led.hpp"
#include "slstm32/drivers/serial_log.hpp"
#include "slstm32/drivers/tone.hpp"
#include "slstm32/event_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

// Seam between product code and development/test code (AGENTS.md, "Firmware
// code categories and build profiles"). The composition root always binds
// devTools(); the build decides what it is:
// - with development tools: src/dev/dev_tools.cpp runs the hardware and input
//   module tests, the background load, and the "trap" and "i2c" commands;
// - without: src/dev_tools_disabled.cpp returns a service that does nothing.
// Product code must not depend on anything inside src/dev/.
namespace lab_test_01 {

// Everything the development tools may use, filled in by the composition root.
struct DevToolsContext {
    slstm32::Runtime runtime;
    slstm32::EventBus& events;
    slstm32::Logger log;
    slstm32::drivers::SerialLogDriver& serialLog;
    slstm32::CommandConsole& console;
    slstm32::drivers::ButtonDriver& buttons;
    std::uint8_t buttonCount;
    slstm32::drivers::PwmLedDriver& greenLed;
    slstm32::drivers::PwmLedDriver& redLed;
    slstm32::drivers::ToneDriver& buzzer;
    slstm32::drivers::I2cBus& i2c;
    can_mcu::I2cScanService& i2cScan;
    semantic_modules::AdInputIso12v& inputModule;
    n2k::Node& n2kNode;
    can_mcu::HealthService& health;
};

slstm32::Service& devTools(const DevToolsContext& context);

} // namespace lab_test_01
