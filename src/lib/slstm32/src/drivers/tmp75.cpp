#include "slstm32/drivers/tmp75.hpp"

namespace slstm32::drivers {

namespace {
constexpr std::uint8_t temperatureRegister = 0x00u;
constexpr std::uint8_t configurationRegister = 0x01u;
constexpr std::uint8_t resolution12Bit = 0x60u; // R1 = R0 = 1, continuous conversion
constexpr std::uint8_t powerOnDefault = 0x00u;  // continuous conversion on all LM75-type parts
} // namespace

bool Tmp75Driver::init() {
    if (!runtime_.millis) return false;
    configured_ = configure();
    nextSampleAt_ = runtime_.millis() + samplePeriodMs_;
    return true;
}

bool Tmp75Driver::configure() {
    if (!bus_.writeRegister(address_, configurationRegister, resolution12Bit)) return false;
    std::uint8_t readBack{};
    if (!bus_.readRegister(address_, configurationRegister, &readBack, 1u)) return false;
    defaultConfiguration_ = readBack != resolution12Bit;
    return !defaultConfiguration_ || bus_.writeRegister(address_, configurationRegister, powerOnDefault);
}

bool Tmp75Driver::sample() {
    std::uint8_t data[2]{};
    if (!bus_.readRegister(address_, temperatureRegister, data, sizeof data)) return false;
    // Left-aligned two's complement: 12 significant bits, 0.0625 degC each.
    const auto raw = static_cast<std::int16_t>(static_cast<std::uint16_t>(data[0] << 8u | data[1]));
    milliCelsius_ = static_cast<std::int32_t>(raw / 16) * 625 / 10;
    return true;
}

void Tmp75Driver::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextSampleAt_) < 0) return;
    nextSampleAt_ = now + samplePeriodMs_;
    if (!configured_) configured_ = configure();
    valid_ = configured_ && sample();
    if (!valid_) {
        ++errors_;
        configured_ = false;
    }
}

} // namespace slstm32::drivers
