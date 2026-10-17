#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"

namespace slstm32::drivers {

// ams/ScioSense AS3935 lightning sensor (I2C address 1..3), for example the
// DFRobot SEN0290 module (address 3 by default, 96 pF antenna tuning).
//
// The interrupt register is polled (the IRQ pin need not be wired): every
// event (lightning with estimated storm distance and raw energy, a
// disturber, or a too high noise level) is passed to the listener. Lightning
// and noise are logged as they come, disturbers as a count every 10 s. With the adaptive noise floor, "noise too high" raises the noise
// floor threshold one step (up to 7); after ten minutes without it, the
// threshold steps back towards the configured level.
//
// Startup follows the datasheet: preset defaults, analog front end gain
// (indoor or outdoor), tuning capacitor, then the RC oscillator calibration,
// whose result is logged. A missing sensor is configured again when it
// answers.
class As3935Driver final : public Driver {
public:
    enum class Location : std::uint8_t { indoor, outdoor };
    struct Config {
        std::uint8_t address{0x03u};
        Location location{Location::indoor};
        std::uint8_t tuningCapacitor{12u}; // 0..15 in 8 pF steps; DFRobot SEN0290: 96 pF
        std::uint8_t noiseFloor{2u};       // 0..7
        bool reportDisturbers{true};
        bool adaptiveNoiseFloor{true};
        std::uint32_t pollMs{50u};
    };

    enum class EventKind : std::uint8_t { lightning, disturber, noiseHigh };
    static constexpr std::uint8_t distanceOutOfRange = 0x3Fu;
    static constexpr std::uint8_t distanceOverhead = 0x01u;
    struct Event {
        EventKind kind{};
        std::uint8_t distanceKm{distanceOutOfRange}; // lightning only: 1 overhead, 5..40 km, 0x3F out of range
        std::uint32_t energy{};                      // lightning only: raw, no physical unit
    };
    using Listener = void (*)(void* context, const Event& event);

    As3935Driver(Runtime runtime, I2cBus& bus, Logger log, const Config& config)
        : runtime_(runtime), bus_(bus), log_(log), config_(config) {}

    bool init() override;
    void run() override;
    void setListener(Listener listener, void* context) {
        listener_ = listener;
        listenerContext_ = context;
    }

    // Antenna tuning: the antenna's resonance (500 kHz +-3.5 %) divided by
    // 16 on the IRQ pin, to be measured with a scope or counter (31.25 kHz).
    // Detection is unreliable while it is shown.
    bool showAntennaFrequency(bool on);
    bool setTuningCapacitor(std::uint8_t steps);
    bool setNoiseFloor(std::uint8_t level);
    bool setLocation(Location location);
    bool setDisturberReporting(bool on);

    bool present() const { return configured_; }
    bool calibrated() const { return calibrated_; }
    std::uint8_t tuningCapacitor() const { return tuningCapacitor_; }
    std::uint8_t noiseFloor() const { return noiseFloor_; }
    Location location() const { return location_; }
    bool disturberReporting() const { return reportDisturbers_; }
    std::uint32_t lightningCount() const { return lightning_; }
    std::uint32_t disturberCount() const { return disturbers_; }
    std::uint32_t noiseHighCount() const { return noiseHigh_; }
    std::uint8_t lastDistanceKm() const { return lastDistanceKm_; }
    std::uint32_t lastEnergy() const { return lastEnergy_; }

private:
    bool readRegister(std::uint8_t reg, std::uint8_t& value);
    bool writeRegister(std::uint8_t reg, std::uint8_t value);
    bool modifyRegister(std::uint8_t reg, std::uint8_t mask, std::uint8_t value);
    bool configure();
    void wait(std::uint32_t ms) const;
    void handle(std::uint8_t interrupt, std::uint32_t now);
    void lost();

    Runtime runtime_;
    I2cBus& bus_;
    Logger log_;
    const Config& config_;
    Listener listener_{};
    void* listenerContext_{};
    std::uint32_t nextAt_{};
    std::uint32_t quietSince_{};
    std::uint32_t disturberLogAt_{};
    std::uint32_t disturbersLogged_{};
    std::uint32_t lightning_{};
    std::uint32_t disturbers_{};
    std::uint32_t noiseHigh_{};
    std::uint32_t lastEnergy_{};
    std::uint8_t lastDistanceKm_{distanceOutOfRange};
    std::uint8_t tuningCapacitor_{};
    std::uint8_t noiseFloor_{};
    Location location_{};
    bool reportDisturbers_{};
    bool configured_{};
    bool calibrated_{};
};

} // namespace slstm32::drivers
