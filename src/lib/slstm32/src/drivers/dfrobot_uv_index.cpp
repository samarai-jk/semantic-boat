#include "slstm32/drivers/dfrobot_uv_index.hpp"

namespace slstm32::drivers {

namespace {

constexpr std::uint8_t registerProductId = 0x00u;
constexpr std::uint8_t registerVersion = 0x05u;
constexpr std::uint8_t registerRaw = 0x06u; // raw, index, risk level follow
constexpr std::uint16_t maximumIndex = 20u;  // plausibility (the scale ends at 11+)

} // namespace

const char* DfrobotUvIndexDriver::riskName(std::uint16_t level) {
    switch (level) {
    case 0u: return "low";
    case 1u: return "moderate";
    case 2u: return "high";
    case 3u: return "very high";
    case 4u: return "extreme";
    }
    return "?";
}

bool DfrobotUvIndexDriver::readRegisters(std::uint8_t first, std::uint16_t* values, std::uint8_t count) {
    std::uint8_t data[6]{};
    if (count > 3u || !bus_.readRegister(address_, first, data, static_cast<std::size_t>(count) * 2u)) return false;
    for (std::uint8_t index = 0u; index < count; ++index) {
        values[index] = static_cast<std::uint16_t>(data[2u * index] | data[2u * index + 1u] << 8u);
    }
    return true;
}

bool DfrobotUvIndexDriver::init() {
    if (!runtime_.millis) return false;
    if (uvIndex_) uvIndex_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 0u));
    nextAt_ = runtime_.millis();
    return true;
}

void DfrobotUvIndexDriver::failed(sensors::Fault fault, std::uint32_t now) {
    if (present_ && fault == sensors::Fault::noResponse) {
        log_.writef(LogLevel::warning, "uv-index 0x%02X: no answer", static_cast<unsigned>(address_));
        present_ = false;
    }
    valid_ = false;
    if (uvIndex_) uvIndex_->fail(fault, now);
}

void DfrobotUvIndexDriver::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    nextAt_ = now + periodMs_;
    if (!present_) {
        std::uint16_t id = 0u;
        std::uint16_t version = 0u;
        if (!readRegisters(registerProductId, &id, 1u)) {
            failed(sensors::Fault::noResponse, now);
            return;
        }
        if (id != productId) {
            log_.writef(LogLevel::warning, "uv-index 0x%02X: product ID 0x%04X, expected 0x%04X",
                        static_cast<unsigned>(address_), static_cast<unsigned>(id), static_cast<unsigned>(productId));
            failed(sensors::Fault::sensorReported, now);
            return;
        }
        (void)readRegisters(registerVersion, &version, 1u);
        log_.writef(LogLevel::info, "uv-index 0x%02X: DFRobot SEN0636, version 0x%04X", static_cast<unsigned>(address_),
                    static_cast<unsigned>(version));
        present_ = true;
    }
    std::uint16_t values[3]{};
    if (!readRegisters(registerRaw, values, 3u)) {
        failed(sensors::Fault::noResponse, now);
        return;
    }
    if (values[1] > maximumIndex || values[2] > 4u) {
        failed(sensors::Fault::outOfRange, now);
        return;
    }
    if (values[2] != riskLevel_ || !valid_) {
        log_.writef(LogLevel::info, "uv-index: UV index %u, risk %s (raw %u)", static_cast<unsigned>(values[1]),
                    riskName(values[2]), static_cast<unsigned>(values[0]));
    }
    raw_ = values[0];
    index_ = values[1];
    riskLevel_ = values[2];
    valid_ = true;
    if (uvIndex_) uvIndex_->update(static_cast<std::int32_t>(index_) * 1000, now);
}

} // namespace slstm32::drivers
