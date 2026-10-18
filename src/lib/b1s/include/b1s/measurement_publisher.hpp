#pragma once

#include <cstddef>
#include <cstdint>
#include "b1s/messages.hpp"
#include "n2k/node.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"
#include "slstm32/service.hpp"

namespace b1s {

// Requested encoding of a channel (README "Standard PGNs"). automatic: the
// standard PGN if the registry lists it as "display", otherwise the B1S
// message. standard falls back to the B1S message if the quantity has no
// standard PGN.
enum class Publish : std::uint8_t { automatic, standard, b1s, off };

// One measurement channel of the device on B1S.
struct ChannelConfig {
    const slstm32::sensors::Measurement* measurement{};
    std::uint8_t channel{};        // B1S channel number, unique on the device, 0..252
    Publish publish{Publish::automatic};
    std::uint8_t standardSource{}; // NMEA 2000 source lookup of the standard PGN (n2k::TemperatureSource, ...)
    const char* location{""};      // Signal K path fragment; "" = unassigned
    const char* label{""};         // for people
};

// Sends measurement channels on B1S once the node has an address:
//
// - every period, each channel as its standard PGN (instance from the
//   channel's MeasurementConfig::instance) or as a B1S Measurement, per its
//   resolved encoding. A channel without any result yet is not sent; a
//   stale or faulted one is sent as "not available" / "error".
// - the Channel Descriptions of all channels after every address claim and
//   on an ISO request for PGN 130819 (register handleRequest with
//   n2k::Node::onRequest), one fast packet per run() call.
//
// At most 32 channels. RAM: a few bytes besides the configuration; the fast
// packet is built on the stack and copied into the node's buffer.
class MeasurementPublisher final : public slstm32::Service {
public:
    static constexpr std::size_t maxChannels = 32u;

    MeasurementPublisher(slstm32::Runtime runtime, n2k::Node& node, const ChannelConfig* channels,
                         std::size_t count, std::uint32_t periodMs = 2000u, slstm32::Logger log = {})
        : runtime_(runtime), node_(node), channels_(channels), count_(count), periodMs_(periodMs), log_(log) {}

    bool init() override;
    void run() override;

    // n2k::RequestHandler: answers requests for Channel Description (130819).
    static bool handleRequest(void* context, std::uint32_t pgn, std::uint8_t requester);

    // How the channel is sent after resolving Publish::automatic.
    static Encoding encoding(const ChannelConfig& channel);

private:
    bool send(const ChannelConfig& channel);
    void describeNext();

    slstm32::Runtime runtime_;
    n2k::Node& node_;
    const ChannelConfig* channels_{};
    std::size_t count_{};
    std::uint32_t periodMs_{};
    slstm32::Logger log_;
    std::uint32_t nextCycleAt_{};
    std::size_t next_{};
    std::uint32_t describePending_{};
    std::uint8_t describedAddress_{n2k::nullAddress};
    std::uint8_t sequenceId_{};
};

} // namespace b1s
