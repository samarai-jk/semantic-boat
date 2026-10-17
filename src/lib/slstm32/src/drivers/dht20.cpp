#include "slstm32/drivers/dht20.hpp"

namespace slstm32::drivers {

namespace {

constexpr std::uint32_t powerUpMs = 100u;
constexpr std::uint32_t conversionMs = 80u;
constexpr std::uint32_t busyRetryMs = 10u;
constexpr std::uint8_t maxBusyRetries = 5u;
constexpr std::uint8_t statusBusy = 0x80u;
constexpr std::uint8_t statusInitialized = 0x18u;

// CRC-8, polynomial 0x31, initial value 0xFF.
std::uint8_t crc8(const std::uint8_t* data, std::size_t size) {
    std::uint8_t crc = 0xFFu;
    for (std::size_t index = 0u; index < size; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            crc = static_cast<std::uint8_t>(crc & 0x80u ? (crc << 1u) ^ 0x31u : crc << 1u);
        }
    }
    return crc;
}

} // namespace

bool Dht20Driver::init() {
    if (!runtime_.millis) return false;
    if (temperature_) temperature_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 0u));
    if (humidity_) humidity_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 1u));
    cycleAt_ = nextAt_ = runtime_.millis() + powerUpMs;
    return true;
}

void Dht20Driver::wait(std::uint32_t ms) const {
    const auto start = runtime_.millis();
    while (runtime_.millis() - start < ms) {
    }
}

// Register initialization from the DHT20 datasheet: read the register and
// write it back with the 0xB0 command prefix.
bool Dht20Driver::resetRegister(std::uint8_t reg) {
    const std::uint8_t select[3]{reg, 0x00u, 0x00u};
    if (!bus_.write(address_, select, sizeof select)) return false;
    wait(5u);
    std::uint8_t value[3]{};
    if (!bus_.read(address_, value, sizeof value)) return false;
    wait(10u);
    const std::uint8_t restore[3]{static_cast<std::uint8_t>(0xB0u | reg), value[1], value[2]};
    return bus_.write(address_, restore, sizeof restore);
}

bool Dht20Driver::prepare() {
    std::uint8_t status = 0u;
    if (!bus_.read(address_, &status, 1u)) return false;
    if ((status & statusInitialized) != statusInitialized) {
        log_.writef(LogLevel::info, "dht20 0x%02X: status 0x%02X, initializing registers",
                    static_cast<unsigned>(address_), static_cast<unsigned>(status));
        if (!resetRegister(0x1Bu) || !resetRegister(0x1Cu) || !resetRegister(0x1Eu)) return false;
        wait(10u);
    }
    const std::uint8_t trigger[3]{0xACu, 0x33u, 0x00u};
    return bus_.write(address_, trigger, sizeof trigger);
}

void Dht20Driver::failed(sensors::Fault fault, std::uint32_t now) {
    ++errors_;
    valid_ = false;
    state_ = State::idle;
    if (present_ && fault == sensors::Fault::noResponse) {
        log_.writef(LogLevel::warning, "dht20 0x%02X: no answer", static_cast<unsigned>(address_));
    }
    if (fault == sensors::Fault::noResponse) present_ = false;
    if (temperature_) temperature_->fail(fault, now);
    if (humidity_) humidity_->fail(fault, now);
}

void Dht20Driver::finish(std::uint32_t now) {
    std::uint8_t data[7]{};
    if (!bus_.read(address_, data, sizeof data)) {
        failed(sensors::Fault::noResponse, now);
        return;
    }
    if (data[0] & statusBusy) {
        if (++busyRetries_ > maxBusyRetries) {
            failed(sensors::Fault::notReady, now);
            return;
        }
        nextAt_ = now + busyRetryMs;
        return;
    }
    if (crc8(data, 6u) != data[6]) {
        failed(sensors::Fault::checksum, now);
        return;
    }
    // 20 bits humidity, 20 bits temperature, both as a fraction of 2^20.
    const std::uint32_t rawHumidity = static_cast<std::uint32_t>(data[1]) << 12u |
                                      static_cast<std::uint32_t>(data[2]) << 4u | data[3] >> 4u;
    const std::uint32_t rawTemperature = (static_cast<std::uint32_t>(data[3]) & 0x0Fu) << 16u |
                                         static_cast<std::uint32_t>(data[4]) << 8u | data[5];
    milliPercent_ = static_cast<std::int32_t>(static_cast<std::uint64_t>(rawHumidity) * 100000u >> 20u);
    milliCelsius_ = static_cast<std::int32_t>(static_cast<std::uint64_t>(rawTemperature) * 200000u >> 20u) - 50000;
    state_ = State::idle;
    if (milliCelsius_ < -40000 || milliCelsius_ > 85000 || milliPercent_ > 100000) {
        failed(sensors::Fault::outOfRange, now);
        return;
    }
    if (!present_) {
        log_.writef(LogLevel::info, "dht20 0x%02X: measuring", static_cast<unsigned>(address_));
        present_ = true;
    }
    valid_ = true;
    ++samples_;
    if (temperature_) temperature_->update(milliCelsius_, now);
    if (humidity_) humidity_->update(milliPercent_, now);
}

void Dht20Driver::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    if (state_ == State::converting) {
        finish(now);
        if (state_ == State::converting) return; // still busy, retry scheduled
        nextAt_ = cycleAt_;
        return;
    }
    cycleAt_ += periodMs_;
    if (static_cast<std::int32_t>(now - cycleAt_) >= 0) cycleAt_ = now + periodMs_;
    if (!prepare()) {
        failed(sensors::Fault::noResponse, now);
        nextAt_ = cycleAt_;
        return;
    }
    state_ = State::converting;
    busyRetries_ = 0u;
    nextAt_ = now + conversionMs;
}

} // namespace slstm32::drivers
