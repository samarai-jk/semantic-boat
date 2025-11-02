#ifndef APP_EVENTS_HPP
#define APP_EVENTS_HPP

#include <cstdint>

namespace app {

enum : uint16_t {
    EVT_INTERNAL_TOGGLE_BUTTON_PRESSED,
    EVT_EXTERNAL_TOGGLE_BUTTON_PRESSED,
    EVT_FUNCTION_BUTTON_PRESSED,
};

} // namespace app

#endif // APP_EVENTS_HPP
