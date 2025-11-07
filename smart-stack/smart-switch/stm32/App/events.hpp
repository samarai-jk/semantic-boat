#ifndef APP_EVENTS_HPP
#define APP_EVENTS_HPP

#include <cstdint>

namespace app {

enum : uint16_t {
    EVT_INTERNAL_TOGGLE_BUTTON_PRESSED,
    EVT_EXTERNAL_TOGGLE_BUTTON_PRESSED,
    EVT_FUNCTION_BUTTON_PRESSED,
    EVT_SMBUS_DATA_RECEIVED,    // payload: uint8_t[] bytes written by master
    EVT_SMBUS_ALERT_ASSERTED,   // payload: none
    EVT_SMBUS_ALERT_RELEASED,   // payload: none
};

} // namespace app

#endif // APP_EVENTS_HPP
