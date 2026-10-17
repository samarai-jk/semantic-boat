#pragma once

#include <cstdint>
#include "slstm32/drivers/one_wire.hpp"

namespace can_mcu {

enum class GpioPort : std::uint8_t { a, b, c, h };

// A GPIO of the STM32L431 as a 1-Wire bus line: open-drain output with the
// internal pull-up enabled (about 40 kOhm, only a backup; the bus needs its
// 4.7 kOhm pull-up). Microsecond timing comes from the DWT cycle counter.
// Keep the object alive as long as the bus uses it.
class OneWirePin {
public:
    OneWirePin(GpioPort port, std::uint8_t pin) : port_(port), mask_(static_cast<std::uint16_t>(1u << pin)) {}
    slstm32::drivers::OneWireHardware hardware() {
        return {this, &configure, &driveLow, &release, &read, &delay};
    }

private:
    static bool configure(void* context);
    static void driveLow(void* context);
    static void release(void* context);
    static bool read(void* context);
    static void delay(std::uint32_t microseconds);

    GpioPort port_;
    std::uint16_t mask_;
};

} // namespace can_mcu
