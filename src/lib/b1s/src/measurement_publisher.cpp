#include "b1s/measurement_publisher.hpp"
#include "b1s/quantity.hpp"
#include "n2k/measurement_publisher.hpp"

namespace b1s {

namespace {

using slstm32::sensors::Fault;
using slstm32::sensors::Status;

constexpr std::uint8_t measurementPriority = 5u;
constexpr std::uint8_t descriptionPriority = 7u;

std::int32_t rawValue(const slstm32::sensors::Measurement& channel) {
    if (channel.valid()) return toRaw(channel.quantity(), channel.value());
    if (channel.status() == Status::fault) {
        if (channel.fault() == Fault::outOfRange) return outOfRange;
        if (channel.fault() != Fault::noResponse) return error;
    }
    return notAvailable;
}

} // namespace

Encoding MeasurementPublisher::encoding(const ChannelConfig& channel) {
    const auto pgn = n2k::standardPgn(channel.measurement->quantity());
    switch (channel.publish) {
    case Publish::off: return Encoding::off;
    case Publish::b1s: return pgn != 0u ? Encoding::b1s : Encoding::noStandard;
    case Publish::standard: return pgn != 0u ? Encoding::standard : Encoding::noStandard;
    case Publish::automatic: break;
    }
    if (pgn == 0u) return Encoding::noStandard;
    return standardCategory(pgn) == StandardCategory::display ? Encoding::standard : Encoding::b1s;
}

bool MeasurementPublisher::init() {
    if (count_ > maxChannels) return false;
    unsigned standard = 0u;
    for (std::size_t index = 0u; index < count_; ++index) {
        if (!channels_[index].measurement) return false;
        if (encoding(channels_[index]) == Encoding::standard) ++standard;
    }
    log_.writef(slstm32::LogLevel::info, "b1s: %u channel(s), %u as standard PGN", static_cast<unsigned>(count_),
                standard);
    nextCycleAt_ = runtime_.millis() + periodMs_;
    next_ = count_;
    return true;
}

void MeasurementPublisher::run() {
    if (!node_.claimed()) return;
    if (node_.address() != describedAddress_) {
        describedAddress_ = node_.address();
        describePending_ = count_ == maxChannels ? 0xFFFFFFFFu : (1u << count_) - 1u;
    }
    describeNext();

    const auto now = runtime_.millis();
    if (next_ >= count_) {
        if (static_cast<std::int32_t>(now - nextCycleAt_) < 0) return;
        nextCycleAt_ += periodMs_;
        if (static_cast<std::int32_t>(now - nextCycleAt_) >= 0) nextCycleAt_ = now + periodMs_;
        next_ = 0u;
        // One sequence id per cycle ties the standard readings of a cycle together.
        sequenceId_ = static_cast<std::uint8_t>((sequenceId_ + 1u) % 253u);
    }
    // A full transmit queue delays the rest of the cycle to the next run().
    while (next_ < count_) {
        if (!send(channels_[next_])) return;
        ++next_;
    }
}

bool MeasurementPublisher::send(const ChannelConfig& channel) {
    const auto& measurement = *channel.measurement;
    if (measurement.status() == Status::unknown) return true;
    std::uint8_t data[8];
    std::uint32_t number = 0u;
    switch (encoding(channel)) {
    case Encoding::off:
        return true;
    case Encoding::standard:
        number = n2k::encodeStandardMeasurement(measurement, channel.standardSource, sequenceId_, data);
        break;
    case Encoding::b1s:
    case Encoding::noStandard:
        encodeMeasurement(data, quantityOf(measurement.quantity()), channel.channel, rawValue(measurement));
        number = pgn::measurement;
        break;
    }
    return node_.sendSingle(measurementPriority, number, n2k::broadcastAddress, data, sizeof data);
}

void MeasurementPublisher::describeNext() {
    if (describePending_ == 0u || node_.fastPacketBusy()) return;
    std::size_t index = 0u;
    while ((describePending_ & (1u << index)) == 0u) ++index;
    const auto& channel = channels_[index];
    const auto encoded = encoding(channel);
    ChannelDescription description{};
    description.channel = channel.channel;
    description.kind = static_cast<std::uint8_t>(quantityOf(channel.measurement->quantity()));
    description.encoding = encoded;
    description.standardInstance = encoded == Encoding::standard ? channel.measurement->config().instance : 0xFFu;
    description.periodCentiseconds = static_cast<std::uint16_t>(periodMs_ / 10u);
    description.location = channel.location;
    description.label = channel.label;
    std::uint8_t data[channelDescriptionCapacity];
    const auto length = encodeChannelDescription(data, description);
    if (node_.sendFast(descriptionPriority, pgn::channelDescription, n2k::broadcastAddress, data, length)) {
        describePending_ &= ~(1u << index);
    }
}

bool MeasurementPublisher::handleRequest(void* context, std::uint32_t requested, std::uint8_t) {
    auto& self = *static_cast<MeasurementPublisher*>(context);
    if (requested != pgn::channelDescription) return false;
    self.describePending_ = self.count_ == maxChannels ? 0xFFFFFFFFu : (1u << self.count_) - 1u;
    return true;
}

} // namespace b1s
