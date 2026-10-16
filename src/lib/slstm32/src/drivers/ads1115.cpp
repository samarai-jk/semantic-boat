#include "slstm32/drivers/ads1115.hpp"

namespace slstm32::drivers {

namespace {

constexpr std::uint8_t conversionRegister = 0x00u;
constexpr std::uint8_t configRegister = 0x01u;
constexpr std::uint16_t startSingleShot = 1u << 15u; // OS: write starts, read 1 = idle
constexpr std::uint16_t singleShotMode = 1u << 8u;
constexpr std::uint16_t comparatorDisabled = 0x0003u;
constexpr std::uint32_t retryPeriodMs = 1000u;
// The internal oscillator may be up to 10 % slow; allow for that before the
// first status poll and give up after a generous timeout.
constexpr std::uint32_t conversionTimeoutMs = 200u;

bool reached(std::uint32_t now, std::uint32_t deadline) {
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

} // namespace

std::int32_t Ads1115Driver::fullScaleMicrovolts(Gain gain) {
    switch (gain) {
    case Gain::fsr6144: return 6144000;
    case Gain::fsr4096: return 4096000;
    case Gain::fsr2048: return 2048000;
    case Gain::fsr1024: return 1024000;
    case Gain::fsr512: return 512000;
    case Gain::fsr256: return 256000;
    }
    return 2048000;
}

std::uint32_t Ads1115Driver::conversionMicroseconds(DataRate rate) {
    switch (rate) {
    case DataRate::sps8: return 125000u;
    case DataRate::sps16: return 62500u;
    case DataRate::sps32: return 31250u;
    case DataRate::sps64: return 15625u;
    case DataRate::sps128: return 7813u;
    case DataRate::sps250: return 4000u;
    case DataRate::sps475: return 2106u;
    case DataRate::sps860: return 1163u;
    }
    return 7813u;
}

std::int32_t Ads1115Driver::microvolts(std::size_t channel) const {
    if (channel >= channelCount_) return 0;
    return static_cast<std::int32_t>(static_cast<std::int64_t>(raw_[channel]) *
                                     fullScaleMicrovolts(channels_[channel].gain) / 32768);
}

bool Ads1115Driver::init() {
    if (!runtime_.millis || !channels_ || channelCount_ == 0u || channelCount_ > maximumChannels) {
        return false;
    }
    const auto now = runtime_.millis();
    present_ = bus_.probe(address_);
    if (present_) {
        (void)startConversion(now);
    } else {
        retryAt_ = now + retryPeriodMs;
    }
    return true;
}

bool Ads1115Driver::startConversion(std::uint32_t now) {
    const auto& channel = channels_[current_];
    const auto config = static_cast<std::uint16_t>(
        startSingleShot | static_cast<std::uint16_t>(static_cast<std::uint16_t>(channel.input) << 12u) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(channel.gain) << 9u) | singleShotMode |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(dataRate_) << 5u) | comparatorDisabled);
    const std::uint8_t frame[3]{configRegister, static_cast<std::uint8_t>(config >> 8u),
                                static_cast<std::uint8_t>(config)};
    if (!bus_.write(address_, frame, sizeof frame)) {
        fail(now);
        return false;
    }
    phase_ = Phase::converting;
    startedAt_ = now;
    return true;
}

void Ads1115Driver::fail(std::uint32_t now) {
    ++errors_;
    present_ = false;
    valid_ = {};
    phase_ = Phase::idle;
    retryAt_ = now + retryPeriodMs;
}

void Ads1115Driver::run() {
    const auto now = runtime_.millis();
    if (phase_ == Phase::idle) {
        if (!reached(now, retryAt_)) return;
        present_ = bus_.probe(address_);
        if (!present_) {
            retryAt_ = now + retryPeriodMs;
            return;
        }
        (void)startConversion(now);
        return;
    }

    const auto conversionMs = conversionMicroseconds(dataRate_) * 11u / 10u / 1000u + 1u;
    if (!reached(now, startedAt_ + conversionMs)) return;
    std::uint8_t data[2]{};
    if (!bus_.readRegister(address_, configRegister, data, sizeof data)) {
        fail(now);
        return;
    }
    if ((data[0] & (startSingleShot >> 8u)) == 0u) {
        // Still converting.
        if (reached(now, startedAt_ + conversionTimeoutMs)) fail(now);
        return;
    }
    if (!bus_.readRegister(address_, conversionRegister, data, sizeof data)) {
        fail(now);
        return;
    }
    raw_[current_] = static_cast<std::int16_t>(static_cast<std::uint16_t>(data[0] << 8u | data[1]));
    valid_[current_] = true;
    ++samples_[current_];
    current_ = current_ + 1u == channelCount_ ? 0u : current_ + 1u;
    (void)startConversion(now);
}

} // namespace slstm32::drivers
