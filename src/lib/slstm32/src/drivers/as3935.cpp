#include "slstm32/drivers/as3935.hpp"

namespace slstm32::drivers {

namespace {

constexpr std::uint8_t registerAfe = 0x00u;        // bits 5:1 AFE_GB, bit 0 PWD
constexpr std::uint8_t registerNoise = 0x01u;      // bits 6:4 NF_LEV, bits 3:0 WDTH
constexpr std::uint8_t registerInterrupt = 0x03u;  // bits 7:6 LCO_FDIV, bit 5 MASK_DIST, bits 3:0 INT
constexpr std::uint8_t registerEnergy = 0x04u;     // 0x04..0x06, 21 bits
constexpr std::uint8_t registerDistance = 0x07u;   // bits 5:0
constexpr std::uint8_t registerTuning = 0x08u;     // bit 7 DISP_LCO, bit 6 DISP_SRCO, bit 5 DISP_TRCO, bits 3:0 TUN_CAP
constexpr std::uint8_t registerTrcoStatus = 0x3Au; // bit 7 done, bit 6 failed
constexpr std::uint8_t registerSrcoStatus = 0x3Bu;
constexpr std::uint8_t commandPresetDefault = 0x3Cu;
constexpr std::uint8_t commandCalibrateRco = 0x3Du;
constexpr std::uint8_t directCommand = 0x96u;

constexpr std::uint8_t afeIndoor = 0x24u;  // AFE_GB 10010
constexpr std::uint8_t afeOutdoor = 0x1Cu; // AFE_GB 01110
constexpr std::uint8_t interruptNoiseHigh = 0x01u;
constexpr std::uint8_t interruptDisturber = 0x04u;
constexpr std::uint8_t interruptLightning = 0x08u;
constexpr std::uint8_t calibrationDone = 0x80u;
constexpr std::uint8_t calibrationFailed = 0x40u;

constexpr std::uint32_t retryMs = 1000u;
constexpr std::uint32_t noiseFloorRecoveryMs = 10u * 60u * 1000u;
constexpr std::uint32_t disturberLogMs = 10000u;

} // namespace

bool As3935Driver::readRegister(std::uint8_t reg, std::uint8_t& value) {
    return bus_.readRegister(config_.address, reg, &value, 1u);
}

bool As3935Driver::writeRegister(std::uint8_t reg, std::uint8_t value) {
    return bus_.writeRegister(config_.address, reg, value);
}

bool As3935Driver::modifyRegister(std::uint8_t reg, std::uint8_t mask, std::uint8_t value) {
    std::uint8_t current = 0u;
    if (!readRegister(reg, current)) return false;
    return writeRegister(reg, static_cast<std::uint8_t>((current & ~mask) | (value & mask)));
}

void As3935Driver::wait(std::uint32_t ms) const {
    const auto start = runtime_.millis();
    // One extra millisecond: the tick may advance right after the start.
    while (runtime_.millis() - start <= ms) {
    }
}

bool As3935Driver::init() {
    if (!runtime_.millis) return false;
    tuningCapacitor_ = static_cast<std::uint8_t>(config_.tuningCapacitor & 0x0Fu);
    noiseFloor_ = static_cast<std::uint8_t>(config_.noiseFloor & 0x07u);
    location_ = config_.location;
    reportDisturbers_ = config_.reportDisturbers;
    pollMs_ = config_.pollMs ? config_.pollMs : 1u;
    nextAt_ = runtime_.millis();
    return true;
}

// Blocks about 10 ms (datasheet waits), once per power-up or reconnection.
bool As3935Driver::configure() {
    if (!writeRegister(commandPresetDefault, directCommand)) return false;
    wait(2u);
    const bool ok = writeRegister(registerAfe, location_ == Location::outdoor ? afeOutdoor : afeIndoor) &&
                    modifyRegister(registerNoise, 0x70u, static_cast<std::uint8_t>(noiseFloor_ << 4u)) &&
                    modifyRegister(registerInterrupt, 0x20u, reportDisturbers_ ? 0x00u : 0x20u) &&
                    writeRegister(registerTuning, tuningCapacitor_) &&
                    writeRegister(commandCalibrateRco, directCommand);
    if (!ok) return false;
    wait(2u);
    // The SRCO is calibrated while it is shown on the IRQ pin.
    if (!writeRegister(registerTuning, static_cast<std::uint8_t>(0x40u | tuningCapacitor_))) return false;
    wait(2u);
    if (!writeRegister(registerTuning, tuningCapacitor_)) return false;
    std::uint8_t trco = 0u;
    std::uint8_t srco = 0u;
    if (!readRegister(registerTrcoStatus, trco) || !readRegister(registerSrcoStatus, srco)) return false;
    calibrated_ = (trco & calibrationDone) && (srco & calibrationDone) && !(trco & calibrationFailed) &&
                  !(srco & calibrationFailed);
    log_.writef(calibrated_ ? LogLevel::info : LogLevel::warning,
                "as3935 0x%02X: %s, tuning %u pF, noise floor %u, disturbers %s, oscillator calibration %s "
                "(TRCO 0x%02X, SRCO 0x%02X)",
                static_cast<unsigned>(config_.address), location_ == Location::outdoor ? "outdoor" : "indoor",
                static_cast<unsigned>(tuningCapacitor_) * 8u, static_cast<unsigned>(noiseFloor_),
                reportDisturbers_ ? "reported" : "masked", calibrated_ ? "ok" : "FAILED", static_cast<unsigned>(trco),
                static_cast<unsigned>(srco));
    return true;
}

void As3935Driver::lost() {
    if (configured_) log_.writef(LogLevel::warning, "as3935 0x%02X: no answer", static_cast<unsigned>(config_.address));
    configured_ = false;
    calibrated_ = false;
}

void As3935Driver::handle(std::uint8_t interrupt, std::uint32_t now) {
    Event event{};
    if (interrupt & interruptLightning) {
        std::uint8_t distance = distanceOutOfRange;
        std::uint8_t energy[3]{};
        (void)readRegister(registerDistance, distance);
        (void)bus_.readRegister(config_.address, registerEnergy, energy, sizeof energy);
        event.kind = EventKind::lightning;
        event.distanceKm = static_cast<std::uint8_t>(distance & 0x3Fu);
        event.energy = static_cast<std::uint32_t>(energy[0]) | static_cast<std::uint32_t>(energy[1]) << 8u |
                       static_cast<std::uint32_t>(energy[2] & 0x1Fu) << 16u;
        lastDistanceKm_ = event.distanceKm;
        lastEnergy_ = event.energy;
        ++lightning_;
        if (event.distanceKm == distanceOutOfRange) {
            log_.writef(LogLevel::warning, "as3935: lightning, storm out of range, energy %lu",
                        static_cast<unsigned long>(event.energy));
        } else if (event.distanceKm == distanceOverhead) {
            log_.writef(LogLevel::warning, "as3935: lightning, storm overhead, energy %lu",
                        static_cast<unsigned long>(event.energy));
        } else {
            log_.writef(LogLevel::warning, "as3935: lightning, storm about %u km away, energy %lu",
                        static_cast<unsigned>(event.distanceKm), static_cast<unsigned long>(event.energy));
        }
    } else if (interrupt & interruptDisturber) {
        event.kind = EventKind::disturber;
        ++disturbers_;
    } else if (interrupt & interruptNoiseHigh) {
        event.kind = EventKind::noiseHigh;
        ++noiseHigh_;
        quietSince_ = now;
        if (config_.adaptiveNoiseFloor && noiseFloor_ < 7u &&
            modifyRegister(registerNoise, 0x70u, static_cast<std::uint8_t>((noiseFloor_ + 1u) << 4u))) {
            ++noiseFloor_;
            log_.writef(LogLevel::info, "as3935: noise level too high, noise floor raised to %u",
                        static_cast<unsigned>(noiseFloor_));
        } else {
            log_.writef(LogLevel::info, "as3935: noise level too high (noise floor %u)",
                        static_cast<unsigned>(noiseFloor_));
        }
    } else {
        return;
    }
    if (listener_) listener_(listenerContext_, event);
}

void As3935Driver::run() {
    const auto now = runtime_.millis();
    if (static_cast<std::int32_t>(now - nextAt_) < 0) return;
    nextAt_ = now + pollMs_;
    if (!configured_) {
        if (!configure()) {
            lost();
            nextAt_ = now + retryMs;
            return;
        }
        configured_ = true;
        quietSince_ = now;
        disturberLogAt_ = now;
        return;
    }
    std::uint8_t interrupt = 0u;
    if (!readRegister(registerInterrupt, interrupt)) {
        lost();
        nextAt_ = now + retryMs;
        return;
    }
    ++polls_;
    handle(static_cast<std::uint8_t>(interrupt & 0x0Fu), now);
    if (static_cast<std::uint32_t>(now - disturberLogAt_) >= disturberLogMs) {
        if (disturbers_ != disturbersLogged_) {
            log_.writef(LogLevel::info, "as3935: %lu disturber(s) (man-made noise pulses) in %lu s, %lu in total",
                        static_cast<unsigned long>(disturbers_ - disturbersLogged_),
                        static_cast<unsigned long>((now - disturberLogAt_) / 1000u),
                        static_cast<unsigned long>(disturbers_));
            disturbersLogged_ = disturbers_;
        }
        disturberLogAt_ = now;
    }
    if (config_.adaptiveNoiseFloor && noiseFloor_ > (config_.noiseFloor & 0x07u) &&
        static_cast<std::uint32_t>(now - quietSince_) >= noiseFloorRecoveryMs &&
        modifyRegister(registerNoise, 0x70u, static_cast<std::uint8_t>((noiseFloor_ - 1u) << 4u))) {
        --noiseFloor_;
        quietSince_ = now;
        log_.writef(LogLevel::info, "as3935: quiet for 10 min, noise floor lowered to %u",
                    static_cast<unsigned>(noiseFloor_));
    }
}

bool As3935Driver::showAntennaFrequency(bool on) {
    // LCO_FDIV 0 (divide by 16) is the preset default.
    return modifyRegister(registerTuning, 0x80u, on ? 0x80u : 0x00u);
}

bool As3935Driver::setTuningCapacitor(std::uint8_t steps) {
    if (steps > 15u || !modifyRegister(registerTuning, 0x0Fu, steps)) return false;
    tuningCapacitor_ = steps;
    return true;
}

bool As3935Driver::setNoiseFloor(std::uint8_t level) {
    if (level > 7u || !modifyRegister(registerNoise, 0x70u, static_cast<std::uint8_t>(level << 4u))) return false;
    noiseFloor_ = level;
    quietSince_ = runtime_.millis();
    return true;
}

bool As3935Driver::setLocation(Location location) {
    if (!modifyRegister(registerAfe, 0x3Eu, location == Location::outdoor ? afeOutdoor : afeIndoor)) return false;
    location_ = location;
    return true;
}

bool As3935Driver::setDisturberReporting(bool on) {
    if (!modifyRegister(registerInterrupt, 0x20u, on ? 0x00u : 0x20u)) return false;
    reportDisturbers_ = on;
    return true;
}

} // namespace slstm32::drivers
