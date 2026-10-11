#include "slstm32/drivers/latching_relay.hpp"

namespace slstm32::drivers {

void LatchingRelayDriver::write(const DigitalOutputHardware& output, bool value) {
    output.write(output.context, output.activeHigh ? value : !value);
}

bool LatchingRelayDriver::init() {
    if (!setCoil_.write || !resetCoil_.write || !runtime_.millis || pulseMs_ == 0u) return false;
    if (setCoil_.configure && !setCoil_.configure(setCoil_.context)) return false;
    if (resetCoil_.configure && !resetCoil_.configure(resetCoil_.context)) return false;
    write(setCoil_, false);
    write(resetCoil_, false);
    return true;
}

void LatchingRelayDriver::set(bool state) {
    requestedState_ = state;
    requestPending_ = pulsing_ ? requestedState_ != pulseState_ : requestedState_ != state_;
}

void LatchingRelayDriver::run() {
    const auto now = runtime_.millis();
    if (pulsing_ && static_cast<std::int32_t>(now - pulseEndsAt_) >= 0) {
        write(setCoil_, false);
        write(resetCoil_, false);
        pulsing_ = false;
        state_ = pulseState_;
        requestPending_ = requestedState_ != state_;
    }
    if (!pulsing_ && requestPending_) {
        requestPending_ = false;
        pulseState_ = requestedState_;
        write(pulseState_ ? setCoil_ : resetCoil_, true);
        pulseEndsAt_ = now + pulseMs_;
        pulsing_ = true;
    }
}

} // namespace slstm32::drivers
