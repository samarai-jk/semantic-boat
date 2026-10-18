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
    using slstm32::sensors::Quantity;
    using slstm32::sensors::Status;
    const auto* channel = publication.channel;
    if (!channel || channel->status() == Status::unknown) return true;
    const bool available = channel->valid();
    const auto instance = channel->config().instance;
    std::uint8_t data[8];
    std::uint32_t pgn = 0u;
    switch (channel->quantity()) {
    case Quantity::temperature: {
        const auto source = static_cast<TemperatureSource>(publication.source);
        if (available) {
            encodeTemperatureExtendedRange(data, sequenceId_, instance, source, channel->value());
        } else {
            encodeTemperatureNotAvailable(data, sequenceId_, instance, source);
        }
        pgn = pgn::temperatureExtendedRange;
        break;
    }
    case Quantity::relativeHumidity:
        encodeHumidity(data, sequenceId_, instance, static_cast<HumiditySource>(publication.source),
                       available ? channel->value() : humidityNotAvailable);
        pgn = pgn::humidity;
        break;
    case Quantity::pressure:
        // Channel values are 0.001 hPa = 0.1 Pa, the PGN's resolution.
        encodeActualPressure(data, sequenceId_, instance, static_cast<PressureSource>(publication.source),
                             available ? channel->value() : pressureNotAvailable);
        pgn = pgn::actualPressure;
        break;
    case Quantity::rotationalSpeed:
        // The channel instance is the engine instance (0 = single or port engine).
        encodeEngineRapidUpdate(data, instance, available ? channel->value() : engineSpeedNotAvailable);
        pgn = pgn::engineRapidUpdate;
        break;
    default:
        return true;
    }
    return node_.sendSingle(environmentPriority, pgn, broadcastAddress, data, sizeof data);
}

} // namespace n2k
