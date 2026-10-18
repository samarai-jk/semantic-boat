#pragma once

#include <cstddef>
#include <cstdint>
#include "n2k/node.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"
#include "slstm32/service.hpp"

namespace n2k {

// One measurement channel on the bus. The instance is the channel's
// instance; source is the NMEA 2000 source lookup of its quantity
// (TemperatureSource for temperatures, HumiditySource for humidity,
// PressureSource for pressure).
struct MeasurementPublication {
    const slstm32::sensors::Measurement* channel{};
    std::uint8_t source{};
};

// Standard PGN of a measurement quantity: 130316 for temperature, 130313
// for relative humidity, 130314 for pressure, 127488 for rotational speed
// (engine speed); 0 if the quantity has none.
std::uint32_t standardPgn(slstm32::sensors::Quantity quantity);

// Encodes the channel's latest value (or "not available" if the channel is
// not valid) as its standard PGN with the given instance (usually the
// channel's MeasurementConfig::instance) and source lookup (see
// MeasurementPublication). Returns the PGN, or 0 if the quantity has none.
std::uint32_t encodeStandardMeasurement(const slstm32::sensors::Measurement& channel, std::uint8_t instance,
                                        std::uint8_t source, std::uint8_t sequenceId, std::uint8_t (&out)[8]);

// Sends measurement channels periodically once the node has an address:
// temperatures as PGN 130316, relative humidity as PGN 130313, pressure as
// PGN 130314, rotational speed as PGN 127488 (engine speed; the publisher's
// period applies, a dedicated engine device would send it every 100 ms). A channel
// that faulted or went stale is sent as "not available", so displays show
// the failure instead of a frozen value; a channel without any result yet is
// not sent. Quantities without a PGN mapping are skipped. A full transmit
// queue delays the rest of a cycle to the following run() calls.
class MeasurementPublisher final : public slstm32::Service {
public:
    MeasurementPublisher(slstm32::Runtime runtime, Node& node, const MeasurementPublication* publications,
                         std::size_t count, std::uint32_t periodMs = 2000u)
        : runtime_(runtime), node_(node), publications_(publications), count_(count), periodMs_(periodMs) {}

    bool init() override;
    void run() override;

private:
    bool send(const MeasurementPublication& publication);

    slstm32::Runtime runtime_;
    Node& node_;
    const MeasurementPublication* publications_{};
    std::size_t count_{};
    std::uint32_t periodMs_{};
    std::uint32_t nextCycleAt_{};
    std::size_t next_{};
    std::uint8_t sequenceId_{};
};

} // namespace n2k
