#include "one_wire_command.hpp"

namespace lab_test_01 {

namespace {

using slstm32::LogLevel;
using slstm32::drivers::OneWireBus;

struct Context {
    const OneWirePort* ports{};
    std::size_t count{};
    const slstm32::CommandConsole* console{};
};
Context context{};

void probe(const slstm32::Logger& log, const OneWirePort& port) {
    auto& bus = *port.bus;
    for (const std::uint32_t resetUs : {480u, 960u}) {
        const auto profile = bus.presenceProfile(resetUs);
        log.writef(LogLevel::info, "onewire %s: reset %lu us, line %s while driven; after it low from %u to %u us (%u samples)",
                   port.name, static_cast<unsigned long>(resetUs), profile.drivenLow ? "low" : "HIGH",
                   static_cast<unsigned>(profile.firstLowUs), static_cast<unsigned>(profile.lastLowUs),
                   static_cast<unsigned>(profile.lowSamples));
        bus.idleHigh();
    }
    const bool idle = bus.idleHigh();
    const bool present = idle && bus.reset();
    if (!present) {
        log.writef(LogLevel::info, "onewire %s: idle %s, no presence pulse", port.name, idle ? "high" : "LOW");
        return;
    }
    bus.writeByte(0x33u); // Read ROM
    std::uint8_t rom[8];
    bus.read(rom, sizeof rom);
    const bool crc = OneWireBus::crc8(rom, 7u) == rom[7];
    log.writef(LogLevel::info, "onewire %s: idle high, presence, ROM %02X %02X %02X %02X %02X %02X %02X %02X (CRC %s)",
               port.name, rom[0], rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7], crc ? "ok" : "BAD");
}

void command(void*, std::size_t, const char* const*) {
    for (std::size_t index = 0u; index < context.count; ++index) {
        probe(context.console->log(), context.ports[index]);
    }
}

} // namespace

bool registerOneWireCommand(slstm32::CommandConsole& console, const OneWirePort* ports, std::size_t count) {
    context = {ports, count, &console};
    return console.add({"onewire", "onewire  line level, presence and ROM code on each 1-Wire bus", &command, nullptr});
}

} // namespace lab_test_01
