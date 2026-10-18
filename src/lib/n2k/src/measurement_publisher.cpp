#include "n2k/measurement_publisher.hpp"
#include "n2k/engine.hpp"
#include "n2k/humidity.hpp"
#include "n2k/pgn.hpp"
#include "n2k/pressure.hpp"
#include "n2k/temperature.hpp"

namespace n2k {

namespace {

constexpr std::uint8_t environmentPriority = 5u;

} // namespace

bool MeasurementPublisher::init() {
    nextCycleAt_ = runtime_.millis() + periodMs_;
    next_ = count_;
    return true;
}

void MeasurementPublisher::run() {
    const auto now = runtime_.millis();
    if (next_ >= count_) {
        if (static_cast<std::int32_t>(now - nextCycleAt_) < 0) return;
        nextCycleAt_ += periodMs_;
        if (static_cast<std::int32_t>(now - nextCycleAt_) >= 0) nextCycleAt_ = now + periodMs_;
        if (!node_.claimed()) return;
        next_ = 0u;
        // One sequence id per cycle ties the readings of a cycle together.
        sequenceId_ = static_cast<std::uint8_t>((sequenceId_ + 1u) % 253u);
    }
    while (next_ < count_ && node_.claimed()) {
        if (!send(publications_[next_])) return;
        ++next_;
    }
    next_ = count_;
}

bool MeasurementPublisher::send(const MeasurementPublication& publication) {
    const auto* channel = publication.channel;
    if (!channel || channel->status() == slstm32::sensors::Status::unknown) return true;
    std::uint8_t data[8];
    const auto pgn = encodeStandardMeasurement(*channel, publication.source, sequenceId_, data);
    if (pgn == 0u) return true;
    return node_.sendSingle(environmentPriority, pgn, broadcastAddress, data, sizeof data);
}

std::uint32_t standardPgn(slstm32::sensors::Quantity quantity) {
    using slstm32::sensors::Quantity;
    switch (quantity) {
    case Quantity::temperature: return pgn::temperatureExtendedRange;
    case Quantity::relativeHumidity: return pgn::humidity;
    case Quantity::pressure: return pgn::actualPressure;
    case Quantity::rotationalSpeed: return pgn::engineRapidUpdate;
    default: return 0u;
    }
}

std::uint32_t encodeStandardMeasurement(const slstm32::sensors::Measurement& channel, std::uint8_t source,
                                        std::uint8_t sequenceId, std::uint8_t (&out)[8]) {
    using slstm32::sensors::Quantity;
    const bool available = channel.valid();
    const auto instance = channel.config().instance;
    switch (channel.quantity()) {
    case Quantity::temperature: {
        const auto temperatureSource = static_cast<TemperatureSource>(source);
        if (available) {
            encodeTemperatureExtendedRange(out, sequenceId, instance, temperatureSource, channel.value());
        } else {
            encodeTemperatureNotAvailable(out, sequenceId, instance, temperatureSource);
        }
        break;
    }
    case Quantity::relativeHumidity:
        encodeHumidity(out, sequenceId, instance, static_cast<HumiditySource>(source),
                       available ? channel.value() : humidityNotAvailable);
        break;
    case Quantity::pressure:
        // Channel values are 0.001 hPa = 0.1 Pa, the PGN's resolution.
        encodeActualPressure(out, sequenceId, instance, static_cast<PressureSource>(source),
                             available ? channel.value() : pressureNotAvailable);
        break;
    case Quantity::rotationalSpeed:
        // The channel instance is the engine instance (0 = single or port engine).
        encodeEngineRapidUpdate(out, instance, available ? channel.value() : engineSpeedNotAvailable);
        break;
    default:
        return 0u;
    }
    return standardPgn(channel.quantity());
}

} // namespace n2k
