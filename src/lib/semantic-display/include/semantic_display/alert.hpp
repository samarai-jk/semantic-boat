#pragma once

#include <cstdint>

namespace semantic_display {

// Mirrors the four actionable Signal K notification states. The Signal K
// state named "alert" is presented to users as the less ambiguous "info".
enum class AlertLevel : std::uint8_t { info, warning, alarm, emergency };

enum class AlertAction : std::uint8_t { acknowledge, snooze, hideHere, silence };

} // namespace semantic_display
