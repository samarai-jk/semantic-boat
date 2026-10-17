#pragma once

#include "n2k/node.hpp"
#include "slstm32/sensors/attitude_summary.hpp"

namespace n2k {

// Sends one AttitudeSummary as the standard navigation PGNs, all with one
// sequence id: 127250 Vessel Heading (magnetic, "not available" while the
// heading is not usable), 127251 Rate of Turn (mean), 127257 Attitude (mean
// pitch and roll; yaw not available). Meant for a 1 s summary, not for the
// raw sensor rate.
class AttitudePublisher {
public:
    explicit AttitudePublisher(Node& node, std::uint8_t priority = 2u) : node_(node), priority_(priority) {}
    // Returns false if the node has no address or the transmit queue is full.
    bool publish(const slstm32::sensors::AttitudeSummary& summary);

private:
    Node& node_;
    std::uint8_t priority_{};
    std::uint8_t sequenceId_{};
};

} // namespace n2k
