#include "drivers/internal_toggle_ui.hpp"
#include "service.hpp"
#include "events.hpp"
#include "event_bus.hpp"
#include "drivers/internal_toggle_switch_driver.hpp"
#include "driver_factory.hpp"
#include "driver_manager.hpp"

namespace app {

void InternalToggleUi::init() {
    EventBus::instance().subscribe(EVT_TOGGLE_BUTTON_PRESSED,
        [](uint16_t, const void*, uint16_t, void* ctx){
            auto* self = static_cast<InternalToggleUi*>(ctx);
            if (!self) return;
            // Toggle service enable state
            self->setEnabled(!self->isEnabled());
            // Drive LED via switch driver abstraction
            auto* sw = static_cast<InternalToggleSwitchDriver*>(DriverManager::instance().get(DRV_INTERNAL_TOGGLE_SWITCH));
            if (sw) {
                sw->setOutputFlag(self->isEnabled());
            }
        }, this);
}

void InternalToggleUi::run() { /* no periodic work */ }

} // namespace app
