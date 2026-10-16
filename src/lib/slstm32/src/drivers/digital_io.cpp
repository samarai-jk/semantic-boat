#include "slstm32/drivers/digital_input.hpp"
#include "slstm32/drivers/digital_output.hpp"

namespace slstm32::drivers {

bool DigitalInputDriver::readLogical() const {
    const bool physical = hardware_.read(hardware_.context);
    return hardware_.activeHigh ? physical : !physical;
}

bool DigitalInputDriver::init() {
    if (!hardware_.read || (debounceMs_ != 0u && !runtime_.millis)) return false;
    if (hardware_.configure && !hardware_.configure(hardware_.context)) return false;
    // The level found at startup is taken as is, without counting a change.
    value_ = candidate_ = readLogical();
    if (runtime_.millis) candidateSince_ = runtime_.millis();
    return true;
}

void DigitalInputDriver::run() {
    if (!hardware_.read) return;
    const bool level = readLogical();
    if (debounceMs_ == 0u) {
        if (level != value_) ++changes_;
        value_ = level;
        return;
    }
    const auto now = runtime_.millis();
    if (level != candidate_) {
        candidate_ = level;
        candidateSince_ = now;
    }
    if (candidate_ != value_ && static_cast<std::uint32_t>(now - candidateSince_) >= debounceMs_) {
        value_ = candidate_;
        ++changes_;
    }
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
