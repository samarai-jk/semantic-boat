#include <cstdint>
#include "driver_manager.hpp"
#include "driver_factory.hpp"
#include "drivers/rgb_led_driver.hpp"
#include "drivers/ui_hardware_driver.hpp"
#include "services/internal_toggle_ui.hpp"
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
    if (auto* idrv = mgr.get(app::DRV_UI_HARDWARE)) {
        auto* sw = static_cast<app::UiHardwareDriver*>(idrv);
        sw->onInternalToggle();
    }
}

extern "C" void app_mcu_toggle_exti_notify(void) {
    auto& mgr = DriverManager::instance();
    if (auto* idrv = mgr.get(app::DRV_UI_HARDWARE)) {
        auto* sw = static_cast<app::UiHardwareDriver*>(idrv);
        sw->onExternalToggle();
    }
}

extern "C" void app_btn_function_exti_notify(void) {
    auto& mgr = DriverManager::instance();
    if (auto* idrv = mgr.get(app::DRV_UI_HARDWARE)) {
        auto* sw = static_cast<app::UiHardwareDriver*>(idrv);
        sw->onFunctionButton();
    }
}

extern "C" void app_drivers_init(void) {
    
    auto& mgr = DriverManager::instance();

    // Create and register known drivers
    if (auto* rgb = app::createDriver(app::DRV_RGB_LED)) {
        mgr.registerDriver(app::DRV_RGB_LED, rgb);
    }
    if (auto* sw = app::createDriver(app::DRV_UI_HARDWARE)) {
        mgr.registerDriver(app::DRV_UI_HARDWARE, sw);
    }
    if (auto* sw = app::createDriver(app::DRV_DEVICE)) {
        mgr.registerDriver(app::DRV_DEVICE, sw);
    }
    // Register services
    app::ServiceManagerInstance().registerService(new app::InternalToggleUi());

    mgr.initAll();
    app::ServiceManagerInstance().initAll();

}

extern "C" void app_drivers_step(void) {
    auto& mgr = DriverManager::instance();
    mgr.runAll();
    app::ServiceManagerInstance().runAll();
}
