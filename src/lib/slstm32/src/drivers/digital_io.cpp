#include "slstm32/drivers/digital_input.hpp"
#include "slstm32/drivers/digital_output.hpp"

namespace slstm32::drivers {

bool DigitalInputDriver::init() {
    if (!hardware_.read) return false;
    if (hardware_.configure && !hardware_.configure(hardware_.context)) return false;
    run();
    return true;
}

void DigitalInputDriver::run() {
    if (!hardware_.read) return;
    const bool physical = hardware_.read(hardware_.context);
    value_ = hardware_.activeHigh ? physical : !physical;
}

bool DigitalOutputDriver::init() {
    if (!hardware_.write) return false;
    if (hardware_.configure && !hardware_.configure(hardware_.context)) return false;
    write(value_);
    return true;
}

void DigitalOutputDriver::set(bool value) {
    value_ = value;
    write(value_);
}

void DigitalOutputDriver::write(bool value) {
    if (hardware_.write) hardware_.write(hardware_.context, hardware_.activeHigh ? value : !value);
}

} // namespace slstm32::drivers
