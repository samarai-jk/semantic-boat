#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/runtime.hpp"

namespace slstm32::drivers {

// One open-drain GPIO with a pull-up (usually 4.7 kOhm on the sensor side).
// driveLow() pulls the line low, release() lets the pull-up raise it, and
// read() samples the pin level. delayMicroseconds() must busy-wait
// accurately (cycle counter); it is called with interrupts disabled.
struct OneWireHardware {
    void* context{};
    bool (*configure)(void*){};
    void (*driveLow)(void*){};
    void (*release)(void*){};
    bool (*read)(void*){};
    void (*delayMicroseconds)(std::uint32_t){};
};

// Maxim/Dallas 1-Wire master at standard speed, bit-banged on a GPIO.
// Each time slot (about 70 us) runs with interrupts disabled through the
// runtime's critical-section hooks; the 480 us reset pulse does not. A byte
// takes about 0.6 ms, so transfers stay short.
//
// ROM codes are 64-bit values as read from the bus: family code in the low
// byte, 48-bit serial number, CRC in the high byte.
class OneWireBus {
public:
    using Rom = std::uint64_t;

    OneWireBus(Runtime runtime, OneWireHardware hardware) : runtime_(runtime), hardware_(hardware) {}

    bool init();
    // Reset pulse; true if at least one device answered with a presence pulse.
    bool reset();
    // The released line reads high (a missing pull-up or a short reads low).
    bool idleHigh();
    void writeBit(bool bit);
    bool readBit();
    void writeByte(std::uint8_t value);
    std::uint8_t readByte();
    void read(std::uint8_t* data, std::size_t size);

    // Reads the ROM code of the only device on the bus (Read ROM). Fails
    // without a presence pulse or with a CRC error (also when several
    // devices answer at once).
    bool readRom(Rom& rom);
    // Reset and address one device (Match ROM), or all of them (Skip ROM).
    bool select(Rom rom);
    bool selectAll();

    static std::uint8_t crc8(const std::uint8_t* data, std::size_t size);
    static bool romValid(Rom rom);
    static std::uint8_t family(Rom rom) { return static_cast<std::uint8_t>(rom); }

private:
    Runtime runtime_;
    OneWireHardware hardware_;
};

} // namespace slstm32::drivers
