#pragma once

#include <cstdint>
#include "slstm32/drivers/at24c256.hpp"

namespace remote_a {

inline constexpr std::uint8_t eepromI2cAddress = 0x52u;

enum class EepromIoError : std::uint8_t {
    none,
    readFailed,
    writeFailed,
    writeCycleTimeout,
    verifyReadFailed,
    verifyMismatch,
};

bool eepromReady();
void clearEepromIoError();
EepromIoError eepromIoError();
const char* eepromIoErrorMessage();
slstm32::drivers::At24c256Hardware eepromHardware();

} // namespace remote_a
