#include "slstm32/drivers/analog_sensor.hpp"

namespace slstm32::drivers {

namespace {

bool convertLinear(const void* context, std::int32_t input, std::int32_t& value) {
    const auto& line = *static_cast<const LinearCharacteristic*>(context);
    if (line.inputHigh == line.inputLow) return false;
    value = static_cast<std::int32_t>(static_cast<std::int64_t>(input - line.inputLow) *
                                          (line.valueHigh - line.valueLow) / (line.inputHigh - line.inputLow) +
                                      line.valueLow);
    return true;
}

} // namespace

AnalogConversion linearConversion(const LinearCharacteristic& characteristic) {
    return {&convertLinear, &characteristic};
}

bool AnalogSensor::init() {
    if (!runtime_.millis || !input_.sample || !config_.conversion.convert) return false;
    if (input_.configure && !input_.configure(input_.context)) return false;
    if (value_) value_->setIdentity(identity_);
    if (inputChannel_) {
        auto identity = identity_;
        identity.subchannel = static_cast<std::uint8_t>(identity.subchannel + 1u);
        inputChannel_->setIdentity(identity);
    }
    nextAt_ = runtime_.millis() + config_.periodMs;
    return true;
}

void AnalogSensor::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    nextAt_ += config_.periodMs;
    if (static_cast<std::int32_t>(now - nextAt_) >= 0) nextAt_ = now + config_.periodMs;
    std::int32_t input = 0;
    if (!input_.sample(input_.context, input)) return; // no reading yet; staleness covers a stuck input
    lastInput_ = input;
    if (inputChannel_) inputChannel_->update(input, now);
    if (!value_) return;
    std::int32_t value = 0;
    if (input < config_.faultBelow || input > config_.faultAbove ||
        !config_.conversion.convert(config_.conversion.context, input, value)) {
        value_->fail(sensors::Fault::outOfRange, now);
        return;
    }
    value_->update(value, now);
}

} // namespace slstm32::drivers
