#include <cstdint>
#include "driver_manager.hpp"
#include "driver_factory.hpp"
#include "drivers/rgb_led_driver.hpp"
#include "drivers/internal_toggle_ui.hpp"
#include "drivers/internal_toggle_switch_driver.hpp"
#include "service.hpp"

extern "C" {
#include "app.h"
}

namespace {
using app::DriverManager;
using app::IDriver;
}

extern "C" void app_internal_toggle_switch_exti_notify(void) {
    auto& mgr = DriverManager::instance();
    if (auto* idrv = mgr.get(app::DRV_INTERNAL_TOGGLE_SWITCH)) {
        auto* sw = static_cast<app::InternalToggleSwitchDriver*>(idrv);
        sw->onExti();
    }
}

extern "C" void app_drivers_init(void) {
    
    auto& mgr = DriverManager::instance();

    // Create and register known drivers
    if (auto* rgb = app::createDriver(app::DRV_RGB_LED)) {
        mgr.registerDriver(app::DRV_RGB_LED, rgb);
    }
    if (auto* sw = app::createDriver(app::DRV_INTERNAL_TOGGLE_SWITCH)) {
        mgr.registerDriver(app::DRV_INTERNAL_TOGGLE_SWITCH, sw);
    }

    // Register services
    app::ServiceManagerInstance().registerService(new app::InternalToggleUi());

    mgr.initAll();
    app::ServiceManagerInstance().initAll();

    // Example: set a startup color and blink; UI can change it later
    if (auto* idrv = mgr.get(app::DRV_RGB_LED)) {
        auto* rgb = static_cast<app::RgbLedDriver*>(idrv);
        rgb->setColor(0, 8, 0); // dim green
        // Start with static, then UI can change; frequency/duty ignored in Static
        rgb->setProgram(app::RgbLedDriver::Program::Static, 0, 0);
    }
    
}

extern "C" void app_drivers_step(void) {
    auto& mgr = DriverManager::instance();
    mgr.runAll();
    app::ServiceManagerInstance().runAll();
}
