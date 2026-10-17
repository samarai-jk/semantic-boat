#pragma once

#include <cstddef>
#include "slstm32/console.hpp"
#include "slstm32/drivers/one_wire.hpp"

namespace lab_test_01 {

// Service command "onewire": for every 1-Wire bus the idle line level, the
// presence pulse, and the raw ROM code (with its CRC check) of a single
// device. Read-only.
struct OneWirePort {
    const char* name{};
    slstm32::drivers::OneWireBus* bus{};
};

bool registerOneWireCommand(slstm32::CommandConsole& console, const OneWirePort* ports, std::size_t count);

} // namespace lab_test_01
