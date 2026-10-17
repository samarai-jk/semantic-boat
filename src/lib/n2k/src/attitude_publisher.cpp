#include "n2k/attitude_publisher.hpp"
#include "n2k/navigation.hpp"
#include "n2k/pgn.hpp"

namespace n2k {

bool AttitudePublisher::publish(const slstm32::sensors::AttitudeSummary& summary) {
    if (!node_.claimed() || summary.samples == 0u) return false;
    sequenceId_ = static_cast<std::uint8_t>((sequenceId_ + 1u) % 253u);
    std::uint8_t data[8];
    encodeVesselHeading(data, sequenceId_, summary.headingValid ? summary.heading : angleNotAvailable,
                        HeadingReference::magnetic);
    bool ok = node_.sendSingle(priority_, pgn::vesselHeading, broadcastAddress, data, sizeof data);
    encodeRateOfTurn(data, sequenceId_, summary.yawRateMean);
    ok = node_.sendSingle(priority_, pgn::rateOfTurn, broadcastAddress, data, sizeof data) && ok;
    encodeAttitude(data, sequenceId_, angleNotAvailable, summary.pitchMean, summary.rollMean);
    ok = node_.sendSingle(priority_, pgn::attitude, broadcastAddress, data, sizeof data) && ok;
    return ok;
}

} // namespace n2k
