#ifndef APP_DRIVERS_INTERNAL_TOGGLE_UI_HPP
#define APP_DRIVERS_INTERNAL_TOGGLE_UI_HPP

#include <cstdint>
#include "service.hpp"

extern "C" {
#include "main.h"
}

namespace app {

class RgbLedDriver; // forward declaration

// Debounced button toggles LED color/program (uses BTN_FUNCTION pin)
class InternalToggleUi : public IService {
public:
    void init() override;
    void run() override;
};

} // namespace app

#endif // APP_DRIVERS_INTERNAL_TOGGLE_UI_HPP