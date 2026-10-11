#include "slstm32/drivers/pwm_output.hpp"

namespace slstm32::drivers {

namespace {
float clamp(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}
}

bool PwmOutputDriver::init() {
    if (!hardware_.write) return false;
    if (hardware_.configure && !hardware_.configure(hardware_.context)) return false;
    if (hardware_.start && !hardware_.start(hardware_.context)) return false;
    setDuty(duty_);
    return true;
}

void PwmOutputDriver::setDuty(float duty) {
    duty_ = clamp(duty);
    if (hardware_.write) hardware_.write(hardware_.context, duty_);
}

} // namespace slstm32::drivers
