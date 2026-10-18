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

} // namespace b1s
