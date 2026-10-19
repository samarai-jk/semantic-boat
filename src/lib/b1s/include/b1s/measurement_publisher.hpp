#pragma once

#include <cstddef>
#include <cstdint>
#include "b1s/channel_settings.hpp"
#include "b1s/messages.hpp"
#include "n2k/node.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"
#include "slstm32/service.hpp"

namespace b1s {

// Requested encoding of a channel (README "Standard PGNs"). automatic: the
// B1S message (standard PGNs only when chosen explicitly: Signal K files
// them by their source field, next to the boat's NMEA 2000 data). standard
// falls back to the B1S message if the quantity has no standard PGN. The
// values are those of the channelEncoding parameter.
enum class Publish : std::uint8_t { automatic, standard, b1s, off };

// One measurement channel of the device on B1S: the compiled-in defaults.
struct ChannelConfig {
    const slstm32::sensors::Measurement* measurement{};
    std::uint8_t channel{};        // B1S channel number, unique on the device, 0..252
    Publish publish{Publish::automatic};
    std::uint8_t standardSource{}; // source field of the standard PGN (n2k::TemperatureSource, ...); never sea/outside by accident
    const char* location{""};      // Signal K path fragment; "" = unassigned
    const char* label{""};         // for people
    std::uint32_t periodMs{};      // how often the value is sent; 0 = the publisher's period
    std::uint32_t rawPeriodMs{};   // how often its raw value is sent; 0 = never
};

// Send schedule of one channel (RAM provided by the device, one per channel).
struct ChannelState {
    std::uint32_t nextAt{};
    std::uint32_t rawNextAt{};
    std::uint16_t sentSamples{}; // sample count at the last send (low bits)
    std::uint16_t rawSentSamples{};
    bool sent{};
    bool rawSent{};
};

// Sends measurement channels on B1S once the node has an address:
//
// - each channel at its own period, as its standard PGN (instance from the
//   channel's MeasurementConfig::instance unless configured) or as a B1S
//   Measurement, per its resolved encoding. The value is the channel's
//   output (Measurement::output(): last, or average, minimum, maximum over
//   a window). Send times are multiples of the period, so a period equal to
//   an output window sends one value per window. A value goes out only when
//   a new sample arrived since the last send; a channel without any result
//   yet is not sent; a stale or faulted one is sent as "not available" /
//   "error" at every period.
// - optionally the raw value (Measurement::rawValue()) as Measurement Raw at
//   its own period, under the same rules.
// - the Channel Descriptions of all channels after every address claim, of
//   one channel after its configuration changed (describe()), and on an ISO
//   request for PGN 130819 (register handleRequest with
//   n2k::Node::onRequest), one fast packet per run() call.
//
// With ChannelSettings, configuration set over CAN overrides the defaults.
// At most 32 channels, periods 100 ms .. 6553.5 s. RAM: a ChannelState (16
// bytes) per channel; the fast packet is built on the stack and copied into
// the node's buffer.
class MeasurementPublisher final : public slstm32::Service {
public:
    static constexpr std::size_t maxChannels = 32u;
    static constexpr std::uint32_t minimumPeriodMs = 100u;

    template <std::size_t Count>
    MeasurementPublisher(slstm32::Runtime runtime, n2k::Node& node, const ChannelConfig (&channels)[Count],
                         ChannelState (&states)[Count], std::uint32_t periodMs = 2000u, slstm32::Logger log = {},
                         ChannelSettings* settings = nullptr)
        : runtime_(runtime), node_(node), channels_(channels), states_(states), count_(Count), periodMs_(periodMs),
          log_(log), settings_(settings) {}

    bool init() override;
    void run() override;

    // n2k::RequestHandler: answers requests for Channel Description (130819).
    static bool handleRequest(void* context, std::uint32_t pgn, std::uint8_t requester);

    std::size_t size() const { return count_; }
    // The channel with this B1S number, or nullptr.
    const ChannelConfig* find(std::uint8_t channel) const;
    ChannelSettings* settings() const { return settings_; }
    // Effective configuration (defaults and stored overrides).
    Publish publish(const ChannelConfig& channel) const;
    std::uint8_t standardInstance(const ChannelConfig& channel) const;
    std::uint8_t standardSource(const ChannelConfig& channel) const;
    std::uint32_t periodMs(const ChannelConfig& channel) const;
    std::uint32_t rawPeriodMs(const ChannelConfig& channel) const;
    // How the channel is sent after resolving Publish::automatic.
    Encoding encoding(const ChannelConfig& channel) const;
    // Sends the channel's description again and restarts its schedule
    // (after a configuration change).
    void describe(const ChannelConfig& channel);

private:
    enum class Sent : std::uint8_t { sent, skipped, busy };
    Sent send(const ChannelConfig& channel, ChannelState& state, bool raw);
    void describeNext();
    void text(ChannelSettings::Text text, const ChannelConfig& channel, char (&out)[maxTextLength + 1u]) const;

    slstm32::Runtime runtime_;
    n2k::Node& node_;
    const ChannelConfig* channels_{};
    ChannelState* states_{};
    std::size_t count_{};
    std::uint32_t periodMs_{};
    slstm32::Logger log_;
    ChannelSettings* settings_{};
    std::size_t next_{}; // first channel to look at in run(): fair when the queue is full
    std::uint32_t describePending_{};
    std::uint8_t describedAddress_{n2k::nullAddress};
    std::uint8_t sequenceId_{};
};

} // namespace b1s
