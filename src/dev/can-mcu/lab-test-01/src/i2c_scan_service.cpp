#include "i2c_scan_service.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lab_test_01 {
namespace {

using slstm32::LogLevel;
using slstm32::drivers::I2cAddressSet;

// A scan blocks the loop for roughly 20 ms.
constexpr std::uint32_t rescanPeriodMs = 5000u;

struct KnownAddress {
    std::uint8_t address;
    const char* parts;
};

// Hints only: many parts share addresses. Board devices are exact.
constexpr KnownAddress knownAddresses[]{
    {0x10u, "VEML7700 light"},
    {0x1Eu, "HMC5883L compass"},
    {0x23u, "BH1750 light"},
    {0x29u, "TSL2591 light / VL53L0X range"},
    {0x38u, "AHT20 temp/humidity"},
    {0x39u, "TSL2561 light / APDS9960"},
    {0x3Cu, "SSD1306 OLED"},
    {0x40u, "SHT20 / HTU21D / HDC1080 / INA219"},
    {0x44u, "SHT3x / SHT4x temp/humidity"},
    {0x45u, "SHT3x temp/humidity"},
    {0x48u, "ADS1115 ADC / TMP102 / LM75"},
    {0x49u, "ADS1115 ADC (ADDR to VDD) / TMP102"},
    {0x4Fu, "can-mcu TMP75 (board temperature)"},
    {0x52u, "can-mcu AT24C256 (EEPROM)"},
    {0x57u, "MAX30102"},
    {0x5Au, "MLX90614 IR temp / CCS811"},
    {0x5Cu, "BH1750 light (ADDR high) / AM2320"},
    {0x61u, "SCD30 CO2"},
    {0x62u, "SCD4x CO2"},
    {0x68u, "MPU6050 IMU / DS3231 RTC"},
    {0x70u, "TCA9548A I2C mux"},
    {0x76u, "BME280 / BMP280 / BME680"},
    {0x77u, "BME280 / BMP280 / BME680"},
};

const char* partsAt(std::uint8_t address) {
    for (const auto& known : knownAddresses) {
        if (known.address == address) return known.parts;
    }
    return "unknown";
}

} // namespace

bool I2cScanService::init() {
    present_ = slstm32::drivers::scanI2c(bus_);
    log_.writef(LogLevel::info, "i2c: %u device(s) found", static_cast<unsigned>(present_.size()));
    for (auto address = I2cAddressSet::firstAddress; address <= I2cAddressSet::lastAddress; ++address) {
        if (present_.contains(address)) logDevice("present", address);
    }
    nextScanAt_ = runtime_.millis() + rescanPeriodMs;
    return true;
}

void I2cScanService::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextScanAt_) < 0) return;
    nextScanAt_ = now + rescanPeriodMs;
    const auto found = slstm32::drivers::scanI2c(bus_);
    if (found == present_) return;
    for (auto address = I2cAddressSet::firstAddress; address <= I2cAddressSet::lastAddress; ++address) {
        const bool before = present_.contains(address);
        const bool after = found.contains(address);
        if (!before && after) logDevice("appeared", address);
        if (before && !after) logDevice("disappeared", address);
    }
    present_ = found;
}

bool I2cScanService::registerCommands(slstm32::CommandConsole& console) {
    return console.add({"i2c", "i2c [read <addr> <reg> [n] | write <addr> <reg> <value>]  bus access", &i2cCommand, this});
}

void I2cScanService::i2cCommand(void* context, std::size_t argc, const char* const* argv) {
    auto& self = *static_cast<I2cScanService*>(context);
    const auto number = [](const char* text, unsigned long maximum, unsigned long& value) {
        char* end = nullptr;
        value = std::strtoul(text, &end, 0);
        return end != text && *end == '\0' && value <= maximum;
    };
    if (argc == 1u) {
        const auto found = slstm32::drivers::scanI2c(self.bus_);
        for (auto address = I2cAddressSet::firstAddress; address <= I2cAddressSet::lastAddress; ++address) {
            if (found.contains(address)) self.logDevice("present", address);
        }
        return;
    }
    unsigned long address{};
    unsigned long reg{};
    if (argc >= 4u && std::strcmp(argv[1], "read") == 0 && number(argv[2], 0x7Fu, address) &&
        number(argv[3], 0xFFu, reg)) {
        unsigned long count = 2u;
        if (argc >= 5u && (!number(argv[4], 16u, count) || count == 0u)) {
            self.log_.write(LogLevel::warning, "i2c read: count must be 1..16");
            return;
        }
        std::uint8_t data[16]{};
        if (!self.bus_.readRegister(static_cast<std::uint8_t>(address), static_cast<std::uint8_t>(reg), data,
                                    count)) {
            self.log_.writef(LogLevel::warning, "i2c 0x%02lX reg 0x%02lX: read failed", address, reg);
            return;
        }
        char text[3u * 16u + 1u]{};
        for (std::size_t index = 0u; index < count; ++index) {
            std::snprintf(text + 3u * index, 4u, " %02X", static_cast<unsigned>(data[index]));
        }
        self.log_.writef(LogLevel::info, "i2c 0x%02lX reg 0x%02lX:%s", address, reg, text);
        return;
    }
    unsigned long value{};
    if (argc == 5u && std::strcmp(argv[1], "write") == 0 && number(argv[2], 0x7Fu, address) &&
        number(argv[3], 0xFFu, reg) && number(argv[4], 0xFFu, value)) {
        const bool ok = self.bus_.writeRegister(static_cast<std::uint8_t>(address), static_cast<std::uint8_t>(reg),
                                                static_cast<std::uint8_t>(value));
        self.log_.writef(ok ? LogLevel::info : LogLevel::warning, "i2c 0x%02lX reg 0x%02lX <- 0x%02lX: %s",
                         address, reg, value, ok ? "ok" : "failed");
        return;
    }
    self.log_.write(LogLevel::warning, "usage: i2c [read <addr> <reg> [n] | write <addr> <reg> <value>]");
}

void I2cScanService::logDevice(const char* event, std::uint8_t address) const {
    log_.writef(LogLevel::info, "i2c 0x%02X %s: %s", static_cast<unsigned>(address), event,
                partsAt(address));
}

} // namespace lab_test_01
