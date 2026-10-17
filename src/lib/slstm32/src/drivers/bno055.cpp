#include "slstm32/drivers/bno055.hpp"

namespace slstm32::drivers {

namespace {

constexpr std::uint8_t registerChipId = 0x00u;     // 0xA0; 0x01..0x03 accelerometer, magnetometer, gyro IDs
constexpr std::uint8_t registerSoftware = 0x04u;   // revision LSB, MSB
constexpr std::uint8_t registerPage = 0x07u;
constexpr std::uint8_t registerData = 0x08u;       // acceleration .. calibration status, 46 bytes
constexpr std::uint8_t registerSelfTest = 0x36u;
constexpr std::uint8_t registerSystemStatus = 0x39u;
constexpr std::uint8_t registerSystemError = 0x3Au;
constexpr std::uint8_t registerUnits = 0x3Bu;
constexpr std::uint8_t registerMode = 0x3Du;
constexpr std::uint8_t registerPower = 0x3Eu;
constexpr std::uint8_t registerTrigger = 0x3Fu;
constexpr std::uint8_t chipId = 0xA0u;
constexpr std::uint8_t modeConfig = 0x00u;
constexpr std::uint8_t triggerReset = 0x20u;
// Windows orientation, degrees, dps, m/s2, Celsius.
constexpr std::uint8_t units = 0x00u;

constexpr std::uint32_t bootPollMs = 50u;
constexpr std::uint32_t bootMinimumMs = 700u;  // the chip ID answers earlier, before the start-up is complete
constexpr std::uint32_t statusCheckMs = 1000u;
constexpr std::uint32_t bootTimeoutMs = 1500u; // datasheet: 650 ms from reset to config mode
constexpr std::uint32_t fusionStartMs = 20u;   // config to any mode: 7 ms
constexpr std::uint32_t retryMs = 1000u;
constexpr std::uint8_t statusFusionRunning = 5u;

std::int16_t word(const std::uint8_t* data) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(data[0] | data[1] << 8u));
}

Bno055Driver::Vector vector(const std::uint8_t* data) {
    return {word(data), word(data + 2), word(data + 4)};
}

} // namespace

const char* Bno055Driver::modeName(Mode mode) {
    switch (mode) {
    case Mode::amg: return "AMG (raw, no fusion)";
    case Mode::imu: return "IMU fusion (no magnetometer)";
    case Mode::compass: return "compass";
    case Mode::m4g: return "M4G";
    case Mode::ndofFmcOff: return "NDOF fusion, fast magnetometer calibration off";
    case Mode::ndof: return "NDOF fusion";
    }
    return "?";
}

void Bno055Driver::setMode(Mode mode) {
    mode_ = mode;
    valid_ = false;
    state_ = State::reset;
    nextAt_ = runtime_.millis();
}

bool Bno055Driver::writeRegister(std::uint8_t reg, std::uint8_t value) {
    return bus_.writeRegister(address_, reg, value);
}

bool Bno055Driver::init() {
    if (!runtime_.millis) return false;
    if (heading_) heading_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 0u));
    if (pitch_) pitch_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 1u));
    if (roll_) roll_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 2u));
    nextAt_ = runtime_.millis();
    return true;
}

void Bno055Driver::failed(std::uint32_t now) {
    ++errors_;
    valid_ = false;
    if (present_) log_.writef(LogLevel::warning, "bno055 0x%02X: no answer", static_cast<unsigned>(address_));
    present_ = false;
    state_ = State::reset;
    nextAt_ = now + retryMs;
    if (heading_) heading_->fail(sensors::Fault::noResponse, now);
    if (pitch_) pitch_->fail(sensors::Fault::noResponse, now);
    if (roll_) roll_->fail(sensors::Fault::noResponse, now);
}

void Bno055Driver::read(std::uint32_t now) {
    std::uint8_t all[46]{};
    if (!bus_.readRegister(address_, registerData, all, sizeof all)) {
        failed(now);
        return;
    }
    // 0x08 acceleration, 0x0E magnetic field, then from 0x14: gyro, 0x1A
    // Euler (heading, roll, pitch), 0x20 quaternion, 0x28 linear
    // acceleration, 0x2E gravity, 0x34 temperature, 0x35 calibration.
    acceleration_ = vector(all + 0);
    magnetic_ = vector(all + 6);
    const std::uint8_t* data = all + 12;
    gyro_ = vector(data + 0);
    euler_ = vector(data + 6);
    for (unsigned index = 0u; index < 4u; ++index) quaternion_[index] = word(data + 12 + 2u * index);
    linear_ = vector(data + 20);
    gravity_ = vector(data + 26);
    temperature_ = static_cast<std::int8_t>(data[32]);
    const auto status = data[33];
    const Calibration calibration{static_cast<std::uint8_t>(status >> 6u), static_cast<std::uint8_t>(status >> 4u & 3u),
                                  static_cast<std::uint8_t>(status >> 2u & 3u), static_cast<std::uint8_t>(status & 3u)};
    if (!(calibration == calibration_) || samples_ == 0u) {
        log_.writef(LogLevel::info, "bno055: calibration system %u, gyroscope %u, accelerometer %u, magnetometer %u (of 3)",
                    static_cast<unsigned>(calibration.system), static_cast<unsigned>(calibration.gyroscope),
                    static_cast<unsigned>(calibration.accelerometer), static_cast<unsigned>(calibration.magnetometer));
        calibration_ = calibration;
    }
    valid_ = true;
    ++samples_;
    // 16 LSB per degree.
    if (heading_) heading_->update(static_cast<std::int32_t>(euler_.x) * 1000 / 16, now);
    if (roll_) roll_->update(static_cast<std::int32_t>(euler_.y) * 1000 / 16, now);
    if (pitch_) pitch_->update(static_cast<std::int32_t>(euler_.z) * 1000 / 16, now);
}

void Bno055Driver::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    switch (state_) {
    case State::reset:
        // A reset also recovers a sensor that was running when we started.
        if (!writeRegister(registerPage, 0u) || !writeRegister(registerTrigger, triggerReset)) {
            failed(now);
            return;
        }
        state_ = State::waitBoot;
        stateSince_ = now;
        nextAt_ = now + bootPollMs * 10u;
        return;
    case State::waitBoot: {
        std::uint8_t id = 0u;
        if (static_cast<std::uint32_t>(now - stateSince_) >= bootMinimumMs &&
            bus_.readRegister(address_, registerChipId, &id, 1u) && id == chipId) {
            state_ = State::configure;
            nextAt_ = now;
        } else if (static_cast<std::uint32_t>(now - stateSince_) >= bootTimeoutMs) {
            failed(now);
        } else {
            nextAt_ = now + bootPollMs;
        }
        return;
    }
    case State::configure: {
        std::uint8_t ids[4]{};
        std::uint8_t software[2]{};
        std::uint8_t selfTest = 0u;
        if (!bus_.readRegister(address_, registerChipId, ids, sizeof ids) ||
            !bus_.readRegister(address_, registerSoftware, software, sizeof software) ||
            !bus_.readRegister(address_, registerSelfTest, &selfTest, 1u) || !writeRegister(registerMode, modeConfig) ||
            !writeRegister(registerPower, 0u) || !writeRegister(registerUnits, units) ||
            !writeRegister(registerTrigger, 0u) || !writeRegister(registerMode, static_cast<std::uint8_t>(mode_))) {
            failed(now);
            return;
        }
        log_.writef(LogLevel::info,
                    "bno055 0x%02X: accelerometer 0x%02X, magnetometer 0x%02X, gyroscope 0x%02X, software %u.%u, "
                    "self test 0x%X (0xF = all ok), %s",
                    static_cast<unsigned>(address_), static_cast<unsigned>(ids[1]), static_cast<unsigned>(ids[2]),
                    static_cast<unsigned>(ids[3]), static_cast<unsigned>(software[1]),
                    static_cast<unsigned>(software[0]), static_cast<unsigned>(selfTest & 0x0Fu), modeName(mode_));
        present_ = true;
        state_ = State::waitFusion;
        stateSince_ = now;
        nextAt_ = now + fusionStartMs;
        return;
    }
    case State::waitFusion: {
        std::uint8_t status = 0u;
        std::uint8_t error = 0u;
        if (!bus_.readRegister(address_, registerSystemStatus, &status, 1u) ||
            !bus_.readRegister(address_, registerSystemError, &error, 1u)) {
            failed(now);
            return;
        }
        // Without fusion (AMG) the chip reports "running without fusion" (6).
        if (status == statusFusionRunning || (mode_ == Mode::amg && status == 6u)) {
            state_ = State::running;
            statusAt_ = now + statusCheckMs;
            nextAt_ = now;
            return;
        }
        if (status == 1u || static_cast<std::uint32_t>(now - stateSince_) >= bootTimeoutMs) {
            log_.writef(LogLevel::warning, "bno055 0x%02X: fusion not running (status %u, error %u)",
                        static_cast<unsigned>(address_), static_cast<unsigned>(status), static_cast<unsigned>(error));
            failed(now);
            return;
        }
        nextAt_ = now + bootPollMs;
        return;
    }
    case State::running:
        nextAt_ = now + periodMs_;
        if (static_cast<std::int32_t>(now - statusAt_) >= 0) {
            // The fusion can stop with a system error; the chip then falls
            // back to config mode and reports zeros.
            statusAt_ = now + statusCheckMs;
            std::uint8_t status[2]{};
            if (!bus_.readRegister(address_, registerSystemStatus, status, sizeof status)) {
                failed(now);
                return;
            }
            if (status[0] != statusFusionRunning && !(mode_ == Mode::amg && status[0] == 6u)) {
                log_.writef(LogLevel::warning, "bno055 0x%02X: fusion stopped (status %u, error %u), restarting",
                            static_cast<unsigned>(address_), static_cast<unsigned>(status[0]),
                            static_cast<unsigned>(status[1]));
                ++errors_;
                valid_ = false;
                state_ = State::reset;
                nextAt_ = now;
                return;
            }
        }
        read(now);
        return;
    }
}

} // namespace slstm32::drivers
