#include "can_mcu/board_temperatures.hpp"

namespace can_mcu {

bool BoardTemperatures::init() {
    board_.setIdentity(boardIdentity);
    die_.setIdentity(mcuIdentity);
    nextAt_ = runtime_.millis();
    return true;
}

void BoardTemperatures::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    nextAt_ += periodMs_;
    if (static_cast<std::int32_t>(now - nextAt_) >= 0) nextAt_ = now + periodMs_;
    if (tmp75_.valid()) {
        board_.update(tmp75_.milliCelsius(), now);
    } else if (tmp75_.errors() != 0u) {
        board_.fail(slstm32::sensors::Fault::noResponse, now);
    }
    // The die temperature needs a few hundred conversions after start;
    // until then the channel stays unknown.
    if (mcu_.temperatureValid()) die_.update(mcu_.dieMilliCelsius(), now);
}

} // namespace can_mcu
