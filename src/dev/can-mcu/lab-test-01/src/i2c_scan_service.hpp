#pragma once

#include <cstdint>
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace lab_test_01 {

// Lists the devices on the I2C bus at startup, then rescans periodically and
// logs devices that appear or disappear, so sensors can be plugged in while
// running. Each address is annotated with the parts commonly found there.
class I2cScanService final : public slstm32::Service {
public:
    I2cScanService(slstm32::Runtime runtime, slstm32::drivers::I2cBus& bus, slstm32::Logger log)
        : runtime_(runtime), bus_(bus), log_(log) {}

    bool init() override;
    void run() override;
    // Scans now and logs every device found.
    void logPresent();

private:
    void logDevice(const char* event, std::uint8_t address) const;

    slstm32::Runtime runtime_;
    slstm32::drivers::I2cBus& bus_;
    slstm32::Logger log_;
    slstm32::drivers::I2cAddressSet present_{};
    std::uint32_t nextScanAt_{};
};

} // namespace lab_test_01
