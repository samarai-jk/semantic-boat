#pragma once

#include <cstdint>
#include "b1s/registry.hpp"
#include "slstm32/sensors/measurement.hpp"

// Mapping between slstm32 measurement channels and B1S quantities.
namespace b1s {

// The B1S quantity of an slstm32 quantity. slstm32's generic "concentration"
// is CO2: the only gas sensor so far (SCD4x) measures CO2. A new slstm32
// quantity needs a case here (the compiler warns) and, if no B1S quantity
// fits, a registry entry.
Quantity quantityOf(slstm32::sensors::Quantity quantity);

// Converts a channel value (thousandths of the slstm32 unit, see
// slstm32/sensors/measurement.hpp) to the raw B1S value (SI unit x scale).
std::int32_t toRaw(slstm32::sensors::Quantity quantity, std::int32_t value);
// The inverse: raw B1S value to the channel's internal unit.
std::int32_t fromRaw(slstm32::sensors::Quantity quantity, std::int32_t raw);
// The same for differences (calibration offsets, rates, trends): only the
// scale, no zero offset (a temperature difference in mK is one in m°C).
std::int32_t toRawDelta(slstm32::sensors::Quantity quantity, std::int32_t value);
std::int32_t fromRawDelta(slstm32::sensors::Quantity quantity, std::int32_t raw);

} // namespace b1s
