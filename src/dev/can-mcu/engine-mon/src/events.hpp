#pragma once

#include "slstm32/event_bus.hpp"

namespace engine_mon {

inline constexpr slstm32::EventId buttonPressed = 1u;
// slstm32::sensors::AlarmEvent on every measurement alarm transition.
inline constexpr slstm32::EventId alarmChanged = 2u;

} // namespace engine_mon
