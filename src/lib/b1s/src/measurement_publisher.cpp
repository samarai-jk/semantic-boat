#include "b1s/measurement_publisher.hpp"
#include "b1s/quantity.hpp"
#include "n2k/measurement_publisher.hpp"

namespace b1s {

namespace {

using slstm32::sensors::Fault;
using slstm32::sensors::Status;

constexpr std::uint8_t measurementPriority = 5u;
constexpr std::uint8_t descriptionPriority = 7u;

bool reached(std::uint32_t now, std::uint32_t at) { return static_cast<std::int32_t>(now - at) >= 0; }

// The next multiple of the period after now.
std::uint32_t nextSlot(std::uint32_t now, std::uint32_t period) { return now - now % period + period; }

std::int32_t wireValue(const slstm32::sensors::Measurement& channel, bool raw) {
    std::int32_t value = 0;
    if (raw ? channel.valid() : channel.output(value)) {
        return toRaw(channel.quantity(), raw ? channel.rawValue() : value);
    }
    if (channel.status() == Status::fault) {
        if (channel.fault() == Fault::outOfRange) return outOfRange;
        if (channel.fault() != Fault::noResponse) return error;
    }
    return notAvailable;
}

} // namespace

const ChannelConfig* MeasurementPublisher::find(std::uint8_t channel) const {
    for (std::size_t index = 0u; index < count_; ++index) {
        if (channels_[index].channel == channel) return &channels_[index];
    }
    return nullptr;
}

Publish MeasurementPublisher::publish(const ChannelConfig& channel) const {
    const auto* stored = settings_ ? settings_->overrideOf(channel.channel) : nullptr;
    if (stored && stored->publish <= static_cast<std::uint8_t>(Publish::off)) return static_cast<Publish>(stored->publish);
    return channel.publish;
}

std::uint8_t MeasurementPublisher::standardInstance(const ChannelConfig& channel) const {
    const auto* stored = settings_ ? settings_->overrideOf(channel.channel) : nullptr;
    if (stored && stored->standardInstance <= 252u) return stored->standardInstance;
    return channel.measurement->config().instance;
}

std::uint8_t MeasurementPublisher::standardSource(const ChannelConfig& channel) const {
    const auto* stored = settings_ ? settings_->overrideOf(channel.channel) : nullptr;
    if (stored && stored->standardSource <= 252u) return stored->standardSource;
    return channel.standardSource;
}

std::uint32_t MeasurementPublisher::periodMs(const ChannelConfig& channel) const {
    const auto* stored = settings_ ? settings_->overrideOf(channel.channel) : nullptr;
    std::uint32_t period = channel.periodMs != 0u ? channel.periodMs : periodMs_;
    if (stored && stored->period != ChannelSettings::periodNotSet && stored->period != 0u) period = stored->period * 100u;
    return period < minimumPeriodMs ? minimumPeriodMs : period;
}

std::uint32_t MeasurementPublisher::rawPeriodMs(const ChannelConfig& channel) const {
    const auto* stored = settings_ ? settings_->overrideOf(channel.channel) : nullptr;
    std::uint32_t period = channel.rawPeriodMs;
    if (stored && stored->rawPeriod != ChannelSettings::periodNotSet) period = stored->rawPeriod * 100u;
    return period != 0u && period < minimumPeriodMs ? minimumPeriodMs : period;
}

void MeasurementPublisher::text(ChannelSettings::Text which, const ChannelConfig& channel,
                                char (&out)[maxTextLength + 1u]) const {
    if (settings_ && settings_->readText(which, channel.channel, out)) return;
    const char* value = which == ChannelSettings::Text::location ? channel.location : channel.label;
    std::size_t length = 0u;
    for (; value && value[length] != '\0' && length < maxTextLength; ++length) out[length] = value[length];
    out[length] = '\0';
}

void MeasurementPublisher::describe(const ChannelConfig& channel) {
    const auto index = static_cast<std::size_t>(&channel - channels_);
    if (index >= count_) return;
    describePending_ |= 1u << index;
    // A new period applies from its next multiple.
    const auto now = runtime_.millis();
    states_[index].nextAt = nextSlot(now, periodMs(channel));
    const auto raw = rawPeriodMs(channel);
    states_[index].rawNextAt = nextSlot(now, raw != 0u ? raw : minimumPeriodMs);
}

Encoding MeasurementPublisher::encoding(const ChannelConfig& channel) const {
    const auto pgn = n2k::standardPgn(channel.measurement->quantity());
    switch (publish(channel)) {
    case Publish::off: return Encoding::off;
    case Publish::b1s: return Encoding::b1s;
    case Publish::standard: return pgn != 0u ? Encoding::standard : Encoding::b1s;
    case Publish::automatic: break;
    }
    return Encoding::b1s;
}

bool MeasurementPublisher::init() {
    if (count_ > maxChannels) return false;
    if (settings_) settings_->load();
    unsigned standard = 0u;
    for (std::size_t index = 0u; index < count_; ++index) {
        if (!channels_[index].measurement) return false;
        if (encoding(channels_[index]) == Encoding::standard) ++standard;
        states_[index] = {};
        describe(channels_[index]);
    }
    describePending_ = 0u; // sent after the address claim
    log_.writef(slstm32::LogLevel::info, "b1s: %u channel(s), %u as standard PGN", static_cast<unsigned>(count_),
                standard);
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
    // A full transmit queue leaves the rest for the next run(), starting there.
    for (std::size_t step = 0u; step < count_; ++step) {
        const auto index = (next_ + step) % count_;
        const auto& channel = channels_[index];
        auto& state = states_[index];
        if (reached(now, state.nextAt)) {
            if (send(channel, state, false) == Sent::busy) {
                next_ = index;
                return;
            }
            const auto period = periodMs(channel);
            state.nextAt += period;
            if (reached(now, state.nextAt)) state.nextAt = nextSlot(now, period);
        }
        const auto rawPeriod = rawPeriodMs(channel);
        if (rawPeriod != 0u && reached(now, state.rawNextAt)) {
            if (send(channel, state, true) == Sent::busy) {
                next_ = index;
                return;
            }
            state.rawNextAt += rawPeriod;
            if (reached(now, state.rawNextAt)) state.rawNextAt = nextSlot(now, rawPeriod);
        }
    }
}

MeasurementPublisher::Sent MeasurementPublisher::send(const ChannelConfig& channel, ChannelState& state, bool raw) {
    const auto& measurement = *channel.measurement;
    if (measurement.status() == Status::unknown) return Sent::skipped;
    const auto encoded = encoding(channel);
    if (encoded == Encoding::off) return Sent::skipped;
    // A valid value goes out once per new sample; a missing one at every period.
    const auto samples = static_cast<std::uint16_t>(measurement.samples());
    if (measurement.valid() && (raw ? state.rawSent && state.rawSentSamples == samples
                                    : state.sent && state.sentSamples == samples)) {
        return Sent::skipped;
    }
    std::uint8_t data[8];
    std::uint32_t number = pgn::measurement;
    if (raw) {
        encodeMeasurement(data, quantityOf(measurement.quantity()), channel.channel, wireValue(measurement, true));
        number = pgn::measurementRaw;
    } else if (encoded == Encoding::standard) {
        sequenceId_ = static_cast<std::uint8_t>((sequenceId_ + 1u) % 253u);
        number = n2k::encodeStandardMeasurement(measurement, standardInstance(channel), standardSource(channel),
                                                sequenceId_, data);
    } else {
        encodeMeasurement(data, quantityOf(measurement.quantity()), channel.channel, wireValue(measurement, false));
    }
    if (!node_.sendSingle(measurementPriority, number, n2k::broadcastAddress, data, sizeof data)) return Sent::busy;
    (raw ? state.rawSentSamples : state.sentSamples) = samples;
    (raw ? state.rawSent : state.sent) = true;
    return Sent::sent;
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
    description.noStandard = n2k::standardPgn(channel.measurement->quantity()) == 0u;
    description.standardInstance = encoded == Encoding::standard ? standardInstance(channel) : 0xFFu;
    description.raw = encoded != Encoding::off && rawPeriodMs(channel) != 0u;
    const auto period = periodMs(channel) / 10u;
    description.periodCentiseconds = static_cast<std::uint16_t>(period < 0xFFFFu ? period : 0xFFFEu);
    char location[maxTextLength + 1u];
    char label[maxTextLength + 1u];
    text(ChannelSettings::Text::location, channel, location);
    text(ChannelSettings::Text::label, channel, label);
    description.location = location;
    description.label = label;
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
