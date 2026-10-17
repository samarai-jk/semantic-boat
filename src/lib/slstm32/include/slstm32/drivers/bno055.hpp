#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/measurement.hpp"

namespace slstm32::drivers {

// Bosch BNO055 9-axis absolute orientation sensor (I2C 0x28, or 0x29), for
// example in the DFRobot SEN0253 10-DOF module. The chip runs its own sensor
// fusion (NDOF: accelerometer, gyroscope, magnetometer at 100 Hz); the driver
// reads all fusion outputs in one 34-byte transfer per period: angular rate,
// Euler angles, quaternion, linear acceleration, gravity, temperature, and
// the calibration status.
//
// Heading, pitch, and roll go into angle channels (thousandths of a degree);
// the other outputs are available as accessors. Heading is only meaningful
// once the magnetometer is calibrated (figure-eight motions); calibration
// status changes are logged (0 = uncalibrated .. 3 = fully calibrated per
// sensor). Startup: reset (about 650 ms, not blocking), chip ID 0xA0,
// internal oscillator, units degrees / m/s2 / dps, then NDOF. A missing
// sensor fails the channels and is reset again when it answers.
class Bno055Driver final : public Driver {
public:
    static constexpr std::uint8_t defaultAddress = 0x28u;
    // Operation modes (OPR_MODE values). ndof: 9-axis fusion with absolute
    // heading; imu: accelerometer and gyroscope only (relative heading, immune
    // to magnetic disturbances); amg: raw sensors, no fusion.
    enum class Mode : std::uint8_t { amg = 0x07u, imu = 0x08u, compass = 0x09u, m4g = 0x0Au, ndofFmcOff = 0x0Bu, ndof = 0x0Cu };
    static const char* modeName(Mode mode);

    struct Vector {
        std::int16_t x{};
        std::int16_t y{};
        std::int16_t z{};
    };
    struct Calibration {
        std::uint8_t system{};
        std::uint8_t gyroscope{};
        std::uint8_t accelerometer{};
        std::uint8_t magnetometer{};
        bool operator==(const Calibration& other) const {
            return system == other.system && gyroscope == other.gyroscope && accelerometer == other.accelerometer &&
                   magnetometer == other.magnetometer;
        }
    };

    Bno055Driver(Runtime runtime, I2cBus& bus, Logger log, sensors::Measurement* heading,
                 sensors::Measurement* pitch, sensors::Measurement* roll, std::uint32_t periodMs = 100u,
                 std::uint8_t busIndex = 0u, std::uint8_t address = defaultAddress)
        : runtime_(runtime), bus_(bus), log_(log), heading_(heading), pitch_(pitch), roll_(roll), periodMs_(periodMs),
          busIndex_(busIndex), address_(address) {}

    using SampleListener = void (*)(void* context, const Bno055Driver& sensor);

    bool init() override;
    void run() override;
    // Called after every successful read (for summaries and motion analysis).
    void setSampleListener(SampleListener listener, void* context) {
        sampleListener_ = listener;
        sampleContext_ = context;
    }
    // Selects the operation mode (default ndof) and restarts the sensor.
    void setMode(Mode mode);
    Mode mode() const { return mode_; }

    bool valid() const { return state_ == State::running && valid_; }
    // Euler angles in 1/16 degree as reported (heading 0..360, roll +-90, pitch +-180).
    std::int16_t heading16() const { return euler_.x; }
    std::int16_t roll16() const { return euler_.y; }
    std::int16_t pitch16() const { return euler_.z; }
    // Raw acceleration in 0.01 m/s2, magnetic field in 1/16 uT.
    const Vector& acceleration() const { return acceleration_; }
    const Vector& magneticField() const { return magnetic_; }
    // Angular rate in 1/16 dps, linear acceleration and gravity in 0.01 m/s2.
    const Vector& angularRate() const { return gyro_; }
    const Vector& linearAcceleration() const { return linear_; }
    const Vector& gravity() const { return gravity_; }
    // Unit quaternion w, x, y, z, 1 = 2^14.
    const std::int16_t* quaternion() const { return quaternion_; }
    std::int8_t temperature() const { return temperature_; }
    const Calibration& calibration() const { return calibration_; }
    std::uint32_t samples() const { return samples_; }
    std::uint32_t errors() const { return errors_; }

private:
    enum class State : std::uint8_t { reset, waitBoot, configure, waitFusion, running };

    bool writeRegister(std::uint8_t reg, std::uint8_t value);
    void failed(std::uint32_t now);
    void read(std::uint32_t now);

    Runtime runtime_;
    I2cBus& bus_;
    Logger log_;
    sensors::Measurement* heading_{};
    sensors::Measurement* pitch_{};
    sensors::Measurement* roll_{};
    std::uint32_t periodMs_{};
    std::uint8_t busIndex_{};
    std::uint8_t address_{};
    State state_{State::reset};
    std::uint32_t nextAt_{};
    std::uint32_t stateSince_{};
    std::uint32_t statusAt_{};
    SampleListener sampleListener_{};
    void* sampleContext_{};
    Mode mode_{Mode::ndof};
    Vector acceleration_{};
    Vector magnetic_{};
    Vector gyro_{};
    Vector euler_{};
    Vector linear_{};
    Vector gravity_{};
    std::int16_t quaternion_[4]{};
    std::int8_t temperature_{};
    Calibration calibration_{};
    std::uint32_t samples_{};
    std::uint32_t errors_{};
    bool present_{};
    bool valid_{};
};

} // namespace slstm32::drivers
