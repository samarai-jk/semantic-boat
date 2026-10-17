#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/one_wire.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"

namespace slstm32::drivers {

// CT1780 (and Maxim MAX31850K) 1-Wire K-type thermocouple converter with
// cold-junction compensation, for example the DFRobot SEN0656 module. One
// converter per bus, externally powered. 1-Wire family code 0x3B.
//
// Scratchpad (MAX31850 layout, as read by DFRobot's library): bytes 0..1
// thermocouple temperature (14 bit, 0.25 C, bit 0 = fault), bytes 2..3
// cold-junction temperature (12 bit, 0.0625 C, bits 0..2 = open circuit,
// short to GND, short to VDD), byte 4 configuration with the address pins
// AD0..AD3 (the module's DIP switches) in the low nibble.
//
// The ROM code is the identity of the thermocouple channel (subchannel 0)
// and the optional cold-junction channel (subchannel 1). A reported fault
// (thermocouple open or shorted) fails the thermocouple channel with
// sensorReported. A missing converter is looked for again every period.
class Ct1780Driver final : public Driver {
public:
    static constexpr std::uint8_t family = 0x3Bu;

    Ct1780Driver(Runtime runtime, OneWireBus& bus, Logger log, sensors::Measurement* thermocouple,
                 sensors::Measurement* coldJunction = nullptr, std::uint32_t periodMs = 2000u)
        : runtime_(runtime), bus_(bus), log_(log), thermocouple_(thermocouple), coldJunction_(coldJunction),
          periodMs_(periodMs) {}

    bool init() override;
    void run() override;

    OneWireBus::Rom rom() const { return rom_; }
    // AD0..AD3 from the configuration register (DIP switches).
    std::uint8_t address() const { return address_; }
    std::uint8_t faultBits() const { return faultBits_; }
    std::uint32_t samples() const { return samples_; }
    std::uint32_t errors() const { return errors_; }

private:
    enum class State : std::uint8_t { idle, converting };

    bool discover(std::uint32_t now);
    bool readScratchpad(std::uint8_t (&data)[9]);
    void finish(std::uint32_t now);
    void failed(sensors::Fault fault, std::uint32_t now);
    void lost();

    Runtime runtime_;
    OneWireBus& bus_;
    Logger log_;
    sensors::Measurement* thermocouple_{};
    sensors::Measurement* coldJunction_{};
    std::uint32_t periodMs_{};
    State state_{State::idle};
    OneWireBus::Rom rom_{};
    OneWireBus::Rom foreignRom_{};
    std::uint32_t nextAt_{};
    std::uint32_t cycleAt_{};
    std::uint32_t samples_{};
    std::uint32_t errors_{};
    std::uint8_t address_{};
    std::uint8_t faultBits_{};
};

} // namespace slstm32::drivers
