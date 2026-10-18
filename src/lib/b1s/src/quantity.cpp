#include "b1s/quantity.hpp"
#include "b1s/messages.hpp"

namespace b1s {

namespace {

using SlQuantity = slstm32::sensors::Quantity;

// The conversions below assume these registry scales.
static_assert(scale(Quantity::temperature) == 0.001, "temperature: mK");
static_assert(scale(Quantity::relativeHumidity) == 1e-6, "relative humidity: 1e-6");
static_assert(scale(Quantity::voltage) == 0.001, "voltage: mV");
static_assert(scale(Quantity::illuminance) == 0.001, "illuminance: mlx");
static_assert(scale(Quantity::uvIndex) == 0.001, "UV index: 0.001");
static_assert(scale(Quantity::pressure) == 0.1, "pressure: 0.1 Pa");
static_assert(scale(Quantity::angle) == 1e-6, "angle: 1e-6 rad");
static_assert(scale(Quantity::co2) == 1e-9, "CO2: 1e-9 mole fraction");
static_assert(scale(Quantity::rotationalSpeed) == 0.001, "rotational speed: mHz");

} // namespace

Quantity quantityOf(SlQuantity quantity) {
    switch (quantity) {
    case SlQuantity::temperature: return Quantity::temperature;
    case SlQuantity::relativeHumidity: return Quantity::relativeHumidity;
    case SlQuantity::voltage: return Quantity::voltage;
    case SlQuantity::illuminance: return Quantity::illuminance;
    case SlQuantity::uvIndex: return Quantity::uvIndex;
    case SlQuantity::pressure: return Quantity::pressure;
    case SlQuantity::angle: return Quantity::angle;
    case SlQuantity::concentration: return Quantity::co2;
    case SlQuantity::rotationalSpeed: return Quantity::rotationalSpeed;
    }
    return static_cast<Quantity>(0u); // invalid code
}

std::int32_t toRaw(SlQuantity quantity, std::int32_t value) {
    return quantity == SlQuantity::temperature ? value + 273150 : toRawDelta(quantity, value); // m°C -> mK
}

std::int32_t fromRaw(SlQuantity quantity, std::int32_t raw) {
    return quantity == SlQuantity::temperature ? raw - 273150 : fromRawDelta(quantity, raw);
}

std::int32_t fromRawDelta(SlQuantity quantity, std::int32_t raw) {
    switch (quantity) {
    case SlQuantity::relativeHumidity: return raw / 10;
    case SlQuantity::angle: return static_cast<std::int32_t>(static_cast<std::int64_t>(raw) * 10000 / 174533);
    case SlQuantity::rotationalSpeed: return raw * 60;
    case SlQuantity::temperature:
    case SlQuantity::voltage:
    case SlQuantity::illuminance:
    case SlQuantity::uvIndex:
    case SlQuantity::pressure:
    case SlQuantity::concentration:
        return raw;
    }
    return raw;
}

std::int32_t toRawDelta(SlQuantity quantity, std::int32_t value) {
    switch (quantity) {
    case SlQuantity::temperature: return value;                          // m°C = mK difference
    case SlQuantity::relativeHumidity: return value * 10;                // 0.001 %RH -> 1e-6
    case SlQuantity::angle:                                              // 0.001° -> 1e-6 rad
        return static_cast<std::int32_t>(static_cast<std::int64_t>(value) * 174533 / 10000);
    case SlQuantity::rotationalSpeed: return value / 60;                 // 0.001 rpm -> mHz
    case SlQuantity::voltage:                                            // mV
    case SlQuantity::illuminance:                                        // mlx
    case SlQuantity::uvIndex:                                            // 0.001
    case SlQuantity::pressure:                                           // 0.001 hPa = 0.1 Pa
    case SlQuantity::concentration:                                      // 0.001 ppm = 1e-9
        return value;
    }
    return notAvailable;
}

} // namespace b1s
