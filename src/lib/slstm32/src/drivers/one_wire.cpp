#include "slstm32/drivers/one_wire.hpp"

namespace slstm32::drivers {

namespace {

// Standard-speed timing (Maxim application note 126), in microseconds.
constexpr std::uint32_t resetLowUs = 480u;
constexpr std::uint32_t presenceSampleUs = 70u;
constexpr std::uint32_t resetRecoveryUs = 410u;
constexpr std::uint32_t writeOneLowUs = 6u;
constexpr std::uint32_t writeOneHighUs = 64u;
constexpr std::uint32_t writeZeroLowUs = 60u;
constexpr std::uint32_t writeZeroHighUs = 10u;
constexpr std::uint32_t readLowUs = 3u;
constexpr std::uint32_t readSampleUs = 10u; // sample before 15 us, with margin for call overhead
constexpr std::uint32_t readRecoveryUs = 53u;

constexpr std::uint8_t commandReadRom = 0x33u;
constexpr std::uint8_t commandMatchRom = 0x55u;
constexpr std::uint8_t commandSkipRom = 0xCCu;

} // namespace

bool OneWireBus::init() {
    if (!hardware_.driveLow || !hardware_.release || !hardware_.read || !hardware_.delayMicroseconds) return false;
    if (hardware_.configure && !hardware_.configure(hardware_.context)) return false;
    hardware_.release(hardware_.context);
    return true;
}

bool OneWireBus::idleHigh() {
    hardware_.release(hardware_.context);
    hardware_.delayMicroseconds(5u);
    return hardware_.read(hardware_.context);
}

bool OneWireBus::reset() {
    if (!idleHigh()) return false;
    hardware_.driveLow(hardware_.context);
    hardware_.delayMicroseconds(resetLowUs);
    bool present;
    {
        CriticalSection critical{runtime_};
        hardware_.release(hardware_.context);
        hardware_.delayMicroseconds(presenceSampleUs);
        present = !hardware_.read(hardware_.context);
    }
    hardware_.delayMicroseconds(resetRecoveryUs);
    return present;
}

void OneWireBus::writeBit(bool bit) {
    CriticalSection critical{runtime_};
    hardware_.driveLow(hardware_.context);
    hardware_.delayMicroseconds(bit ? writeOneLowUs : writeZeroLowUs);
    hardware_.release(hardware_.context);
    hardware_.delayMicroseconds(bit ? writeOneHighUs : writeZeroHighUs);
}

bool OneWireBus::readBit() {
    bool bit;
    {
        CriticalSection critical{runtime_};
        hardware_.driveLow(hardware_.context);
        hardware_.delayMicroseconds(readLowUs);
        hardware_.release(hardware_.context);
        hardware_.delayMicroseconds(readSampleUs);
        bit = hardware_.read(hardware_.context);
    }
    hardware_.delayMicroseconds(readRecoveryUs);
    return bit;
}

void OneWireBus::writeByte(std::uint8_t value) {
    for (unsigned bit = 0u; bit < 8u; ++bit) writeBit((value >> bit) & 1u);
}

std::uint8_t OneWireBus::readByte() {
    std::uint8_t value = 0u;
    for (unsigned bit = 0u; bit < 8u; ++bit) {
        if (readBit()) value = static_cast<std::uint8_t>(value | (1u << bit));
    }
    return value;
}

void OneWireBus::read(std::uint8_t* data, std::size_t size) {
    for (std::size_t index = 0u; index < size; ++index) data[index] = readByte();
}

std::uint8_t OneWireBus::crc8(const std::uint8_t* data, std::size_t size) {
    // Dallas/Maxim CRC-8: polynomial x^8 + x^5 + x^4 + 1, LSB first.
    std::uint8_t crc = 0u;
    for (std::size_t index = 0u; index < size; ++index) {
        auto byte = data[index];
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            const bool mix = (crc ^ byte) & 1u;
            crc = static_cast<std::uint8_t>(crc >> 1u);
            if (mix) crc ^= 0x8Cu;
            byte = static_cast<std::uint8_t>(byte >> 1u);
        }
    }
    return crc;
}

bool OneWireBus::romValid(Rom rom) {
    std::uint8_t bytes[8];
    for (unsigned index = 0u; index < 8u; ++index) bytes[index] = static_cast<std::uint8_t>(rom >> (8u * index));
    // All zeros would pass the CRC: a line held low reads as zeros.
    return rom != 0u && crc8(bytes, 7u) == bytes[7];
}

bool OneWireBus::readRom(Rom& rom) {
    if (!reset()) return false;
    writeByte(commandReadRom);
    std::uint8_t bytes[8];
    read(bytes, sizeof bytes);
    Rom value = 0u;
    for (unsigned index = 0u; index < 8u; ++index) value |= static_cast<Rom>(bytes[index]) << (8u * index);
    if (!romValid(value)) return false;
    rom = value;
    return true;
}

bool OneWireBus::select(Rom rom) {
    if (!reset()) return false;
    writeByte(commandMatchRom);
    for (unsigned index = 0u; index < 8u; ++index) writeByte(static_cast<std::uint8_t>(rom >> (8u * index)));
    return true;
}

bool OneWireBus::selectAll() {
    if (!reset()) return false;
    writeByte(commandSkipRom);
    return true;
}

} // namespace slstm32::drivers
