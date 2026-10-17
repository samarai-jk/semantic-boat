#include "slstm32/drivers/bmp585.hpp"

namespace slstm32::drivers {

namespace {

constexpr std::uint8_t registerChipId = 0x01u;
constexpr std::uint8_t registerRevisionId = 0x02u;
constexpr std::uint8_t registerIntSource = 0x15u;  // bit 0: data ready
constexpr std::uint8_t registerData = 0x1Du;       // temperature XLSB..MSB, pressure XLSB..MSB
constexpr std::uint8_t registerIntStatus = 0x27u;  // bit 0: data ready, bit 4: power-on reset done (clear on read)
constexpr std::uint8_t registerStatus = 0x28u;     // bit 1: NVM ready, bit 2: NVM error
constexpr std::uint8_t registerOsrConfig = 0x36u;  // bit 6 press_en, bits 5:3 osr_p, bits 2:0 osr_t
constexpr std::uint8_t registerOdrConfig = 0x37u;  // bit 7 deep_dis, bits 6:2 odr, bits 1:0 power mode
constexpr std::uint8_t registerCommand = 0x7Eu;
constexpr std::uint8_t commandSoftReset = 0xB6u;
constexpr std::uint8_t chipIdBmp585 = 0x51u;
constexpr std::uint8_t chipIdBmp581 = 0x50u;

constexpr std::uint8_t oversampling = 0x40u | (4u << 3u) | 1u; // pressure on, x16, temperature x2
constexpr std::uint8_t odrStandby = 0x80u | (0x1Cu << 2u);     // deep standby off, standby
constexpr std::uint8_t odrForced = odrStandby | 0x02u;

constexpr std::uint32_t firstPollMs = 10u;
constexpr std::uint32_t pollMs = 2u;
constexpr std::uint32_t timeoutMs = 200u;
constexpr std::int32_t minimumPressure_dPa = 300000;  // 300 hPa
constexpr std::int32_t maximumPressure_dPa = 1250000; // 1250 hPa

} // namespace

bool Bmp585Driver::init() {
    if (!runtime_.millis) return false;
    if (pressure_) pressure_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 0u));
    if (temperature_) temperature_->setIdentity(sensors::Identity::i2c(busIndex_, 0u, address_, 1u));
    cycleAt_ = nextAt_ = runtime_.millis();
    return true;
}

// Blocks about 3 ms (soft reset), once per power-up or reconnection.
bool Bmp585Driver::configure() {
    if (!bus_.writeRegister(address_, registerCommand, commandSoftReset)) return false;
    const auto start = runtime_.millis();
    while (runtime_.millis() - start <= 2u) {
    }
    std::uint8_t id = 0u;
    std::uint8_t revision = 0u;
    std::uint8_t status = 0u;
    std::uint8_t intStatus = 0u;
    if (!bus_.readRegister(address_, registerChipId, &id, 1u) ||
        !bus_.readRegister(address_, registerRevisionId, &revision, 1u) ||
        !bus_.readRegister(address_, registerStatus, &status, 1u) ||
        !bus_.readRegister(address_, registerIntStatus, &intStatus, 1u)) {
        return false;
    }
    // Bosch's driver also waits for "NVM ready" (bit 1); the bench BMP585
    // never sets it but measures correctly, so only an NVM error is fatal.
    if ((id != chipIdBmp585 && id != chipIdBmp581) || (status & 0x04u)) {
        log_.writef(LogLevel::warning, "bmp585 0x%02X: chip ID 0x%02X, status 0x%02X: not a working BMP585/BMP581",
                    static_cast<unsigned>(address_), static_cast<unsigned>(id), static_cast<unsigned>(status));
        return false;
    }
    if (!bus_.writeRegister(address_, registerOdrConfig, odrStandby) ||
        !bus_.writeRegister(address_, registerOsrConfig, oversampling) ||
        !bus_.writeRegister(address_, registerIntSource, 0x01u)) {
        return false;
    }
    std::uint8_t config[2]{};
    if (!bus_.readRegister(address_, registerOsrConfig, config, sizeof config)) return false;
    managed_ = config[0] != oversampling;
    const char* name = id == chipIdBmp585 ? "BMP585" : "BMP581";
    if (managed_) {
        static constexpr std::uint16_t odrHz[]{240, 218, 199, 179, 160, 149, 140, 129, 120, 110, 100,
                                               89,  80,  70,  60,  50,  45,  40,  35,  30,  25,  20,
                                               15,  10,  5,   4,   3,   2,   1,   0,   0,   0};
        const unsigned odr = (config[1] >> 2u) & 0x1Fu;
        log_.writef(LogLevel::info,
                    "bmp585 0x%02X: %s rev 0x%02X managed by its module (configuration not accepted): mode %u, "
                    "%u Hz, pressure x%u, temperature x%u; reading its results",
                    static_cast<unsigned>(address_), name, static_cast<unsigned>(revision),
                    static_cast<unsigned>(config[1] & 0x03u), static_cast<unsigned>(odrHz[odr]),
                    1u << ((config[0] >> 3u) & 0x07u), 1u << (config[0] & 0x07u));
    } else {
        log_.writef(LogLevel::info, "bmp585 0x%02X: %s rev 0x%02X (status 0x%02X, interrupts 0x%02X), pressure x16, "
                    "temperature x2",
                    static_cast<unsigned>(address_), name, static_cast<unsigned>(revision),
                    static_cast<unsigned>(status), static_cast<unsigned>(intStatus));
    }
    return true;
}

void Bmp585Driver::failed(sensors::Fault fault, std::uint32_t now) {
    ++errors_;
    valid_ = false;
    state_ = State::idle;
    if (fault == sensors::Fault::noResponse) {
        if (configured_) log_.writef(LogLevel::warning, "bmp585 0x%02X: no answer", static_cast<unsigned>(address_));
        configured_ = false;
    }
    if (pressure_) pressure_->fail(fault, now);
    if (temperature_) temperature_->fail(fault, now);
}

void Bmp585Driver::finish(std::uint32_t now) {
    std::uint8_t data[6]{};
    if (!bus_.readRegister(address_, registerData, data, sizeof data)) {
        failed(sensors::Fault::noResponse, now);
        return;
    }
    state_ = State::idle;
    // 24-bit two's complement temperature, 1/65536 C; unsigned pressure, 1/64 Pa.
    auto rawTemperature = static_cast<std::int32_t>(static_cast<std::uint32_t>(data[2]) << 16u |
                                                    static_cast<std::uint32_t>(data[1]) << 8u | data[0]);
    if (rawTemperature & 0x800000) rawTemperature -= 0x1000000;
    const std::uint32_t rawPressure = static_cast<std::uint32_t>(data[5]) << 16u |
                                      static_cast<std::uint32_t>(data[4]) << 8u | data[3];
    milliCelsius_ = static_cast<std::int32_t>(static_cast<std::int64_t>(rawTemperature) * 1000 / 65536);
    pressure_dPa_ = static_cast<std::int32_t>(rawPressure * 10u / 64u);
    if (pressure_dPa_ < minimumPressure_dPa || pressure_dPa_ > maximumPressure_dPa) {
        failed(sensors::Fault::outOfRange, now);
        return;
    }
    valid_ = true;
    ++samples_;
    if (pressure_) pressure_->update(pressure_dPa_, now);
    if (temperature_) temperature_->update(milliCelsius_, now);
}

void Bmp585Driver::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    if (state_ == State::converting) {
        std::uint8_t intStatus = 0u;
        if (!bus_.readRegister(address_, registerIntStatus, &intStatus, 1u)) {
            failed(sensors::Fault::noResponse, now);
            nextAt_ = cycleAt_;
            return;
        }
        if (intStatus & 0x01u) {
            conversionMs_ = now - startedAt_;
            finish(now);
            nextAt_ = cycleAt_;
        } else if (static_cast<std::uint32_t>(now - startedAt_) >= timeoutMs) {
            failed(sensors::Fault::notReady, now);
            nextAt_ = cycleAt_;
        } else {
            nextAt_ = now + pollMs;
        }
        return;
    }
    cycleAt_ += periodMs_;
    if (static_cast<std::int32_t>(now - cycleAt_) >= 0) cycleAt_ = now + periodMs_;
    nextAt_ = cycleAt_;
    if (!configured_) {
        if (!configure()) {
            failed(sensors::Fault::noResponse, now);
            return;
        }
        configured_ = true;
    }
    if (managed_) {
        finish(now);
        return;
    }
    if (!bus_.writeRegister(address_, registerOdrConfig, odrForced)) {
        failed(sensors::Fault::noResponse, now);
        return;
    }
    state_ = State::converting;
    startedAt_ = now;
    nextAt_ = now + firstPollMs;
}

} // namespace slstm32::drivers
