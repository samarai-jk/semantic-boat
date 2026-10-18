#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"

namespace slstm32::drivers {

struct FrequencyInputConfig {
    // Fine time base for the edge timestamps (e.g. the CPU cycle counter).
    std::uint32_t (*ticks)(){};
    std::uint32_t ticksPerSecond{};
    // Pulses per unit of the channel value, in thousandths: pulses per
    // revolution for rpm (alternator W terminal: pole pairs x pulley ratio,
    // e.g. 6 x 2.5 = 15000), 1000 for a plain frequency in Hz.
    std::uint32_t milliPulsesPerUnit{1000u};
    // Units per minute (rpm) instead of per second (Hz).
    bool perMinute{true};
    // Edges closer than this are glitches and ignored (sets the maximum
    // frequency). 0 = no filter.
    std::uint32_t minimumPeriodUs{};
    // Update interval of the channel, and how long without an edge reads
    // as 0 (engine stopped).
    std::uint32_t periodMs{250u};
    std::uint32_t timeoutMs{2000u};
};

// Frequency or speed from pulses on a digital input (engine rpm from an
// alternator W terminal or a pickup, a paddle-wheel log, a flow meter). The
// board calls onEdge() from the input's interrupt for one edge direction;
// the driver timestamps it and, every period, computes the frequency from
// all edges of the period (or from the last single period at low
// frequencies) and updates the channel in thousandths of a unit per minute
// or second. No pulses for timeoutMs: the value is 0, still valid.
//
// Status: this driver itself has not run on hardware yet. Its method was
// verified on lab-test-01 with a temporary dev tool (git history, commit
// acdeb37 "rpm frequency test"): TIM2 PWM on PA0 drove an isolated MOSFET
// switch that put 12 V onto a digital input of the 12v-ad-input-iso module,
// and the module output was timestamped by interrupt with the cycle counter.
// Results: 750 Hz (an alternator W terminal at 3000 rpm: 6 pole pairs x 2.5
// pulley ratio) counted without a missing pulse, period resolution about
// +-5 us, also with 0.33 ms gaps and with the module's standard 100 nF input
// filter. Pitfall for repeating the test: isolated "PWM MOSFET" modules
// switch off about 0.9 ms late (optocoupler and gate discharge), so they
// limit the test, not the input; a direct NPN or logic-level MOSFET switch
// with a 1 kOhm pull-up to 12 V is a faster source. To check this driver:
// bind it to a module input (interrupt on the edge where 12 V appears), feed
// a known frequency, compare the channel value with f x 60 / pulses per
// revolution.
class FrequencyInput final : public Driver {
public:
    FrequencyInput(Runtime runtime, const FrequencyInputConfig& config, sensors::Measurement* channel)
        : runtime_(runtime), config_(config), channel_(channel) {}

    bool init() override;
    void run() override;
    // Interrupt context: one call per counted edge.
    void onEdge();

    // Latest frequency in mHz and value in thousandths of a unit.
    std::uint32_t milliHertz() const { return milliHertz_; }
    std::int32_t value() const { return value_; }
    std::uint32_t edges() const { return totalEdges_; }
    std::uint32_t glitches() const { return glitches_; }

private:
    Runtime runtime_;
    const FrequencyInputConfig& config_;
    sensors::Measurement* channel_{};
    std::uint32_t minimumTicks_{};
    std::uint32_t nextAt_{};
    // Written by onEdge().
    volatile std::uint32_t count_{};
    volatile std::uint32_t firstTick_{};
    volatile std::uint32_t lastTick_{};
    volatile std::uint32_t previousTick_{};
    volatile std::uint32_t lastEdgeMs_{};
    volatile std::uint32_t totalEdges_{};
    volatile std::uint32_t glitches_{};
    std::uint32_t milliHertz_{};
    std::int32_t value_{};
};

} // namespace slstm32::drivers
