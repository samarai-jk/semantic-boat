#include "semantic_modules/ad_input_iso_12v_calibration.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "slstm32/storage/redundant_blob_store.hpp"

namespace semantic_modules {
namespace {

using slstm32::LogLevel;

constexpr std::uint8_t firstAddress = 0x48u;
constexpr std::uint8_t recordVersion = 1u;
constexpr std::size_t recordBytes = 2u + 4u * 4u + 4u * 2u;
constexpr std::uint32_t averageSamples = 32u;
constexpr std::uint32_t sessionTimeoutMs = 5000u;
constexpr float maximumScaleDeviation = 0.05f;
constexpr std::int32_t maximumOffsetMv = 100;
constexpr std::int32_t minimumSpanAdcMicrovolts = 100000; // about 0.9 V at the terminal
constexpr std::uint32_t autoTimeoutMs = 600000u;
constexpr std::uint32_t autoSettleMs = 300u;
constexpr std::uint32_t autoSamples = 24u;

bool slotFor(std::uint8_t address, std::size_t& slot) {
    if (address < firstAddress || address >= firstAddress + AdInputIso12vCalibrationStore::slots) {
        return false;
    }
    slot = address - firstAddress;
    return true;
}

// Fixed-point text without floating-point printf: "9.2097" or "-0.006".
// newlib-nano's printf has no 64-bit conversions, so values stay in long.
const char* fixed(char (&buffer)[24], long scaled, unsigned decimals) {
    long divisor = 1;
    for (unsigned index = 0; index < decimals; ++index) divisor *= 10;
    const auto magnitude = scaled < 0 ? -scaled : scaled;
    std::snprintf(buffer, sizeof buffer, "%s%ld.%0*ld", scaled < 0 ? "-" : "", magnitude / divisor,
                  static_cast<int>(decimals), magnitude % divisor);
    return buffer;
}

long scaleTimes10000(float scale) {
    return static_cast<long>(scale * 10000.0f + (scale >= 0.0f ? 0.5f : -0.5f));
}

// Parses "11.972", "12" or "-0.5" volts into millivolts.
bool parseMillivolts(const char* text, std::int32_t& millivolts) {
    bool negative = false;
    if (*text == '-') {
        negative = true;
        ++text;
    }
    if (*text < '0' || *text > '9') return false;
    std::int32_t whole = 0;
    while (*text >= '0' && *text <= '9') {
        whole = whole * 10 + (*text++ - '0');
        if (whole > 1000) return false;
    }
    std::int32_t fraction = 0;
    int digits = 0;
    if (*text == '.') {
        ++text;
        while (*text >= '0' && *text <= '9') {
            if (digits < 3) {
                fraction = fraction * 10 + (*text - '0');
                ++digits;
            }
            ++text;
        }
    }
    if (*text != '\0' && !(*text == 'V' || *text == 'v')) return false;
    while (digits++ < 3) fraction *= 10;
    millivolts = (whole * 1000 + fraction) * (negative ? -1 : 1);
    return true;
}

bool parseAddress(const char* text, std::uint8_t& address) {
    char* end = nullptr;
    const auto value = std::strtoul(text, &end, 0);
    if (end == text || *end != '\0' || value > 0x7Fu) return false;
    address = static_cast<std::uint8_t>(value);
    return true;
}

bool parseChannel(const char* text, std::size_t& channel) {
    if (*text == 'a' || *text == 'A') ++text;
    if (*text == 'i' || *text == 'I') ++text;
    if (text[0] < '0' || text[0] > '3' || text[1] != '\0') return false;
    channel = static_cast<std::size_t>(text[0] - '0');
    return true;
}

void putFloat(std::uint8_t* out, float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    for (unsigned index = 0; index < 4u; ++index) out[index] = static_cast<std::uint8_t>(bits >> (8u * index));
}

float getFloat(const std::uint8_t* in) {
    std::uint32_t bits = 0u;
    for (unsigned index = 0; index < 4u; ++index) bits |= static_cast<std::uint32_t>(in[index]) << (8u * index);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

bool plausibleScale(float scale) {
    const float design = AdInputIso12vCalibration::designScale;
    return scale > design * (1.0f - maximumScaleDeviation) && scale < design * (1.0f + maximumScaleDeviation);
}

} // namespace

// --- Store ----------------------------------------------------------------------

bool AdInputIso12vCalibrationStore::load(std::uint8_t address, AdInputIso12vCalibration& calibration) {
    std::size_t slot{};
    if (!slotFor(address, slot)) return false;
    slstm32::storage::RedundantBlobStore store{storage_, offset_ + static_cast<std::uint32_t>(slot * slotBytes), slotBytes};
    std::uint8_t record[slstm32::storage::RedundantBlobStore::maxPayloadBytes]{};
    std::size_t size = 0u;
    if (!store.load(record, sizeof record, size)) return false;
    if (size < recordBytes || record[0] != recordVersion || record[1] != address) return false;
    AdInputIso12vCalibration loaded{};
    for (std::size_t channel = 0u; channel < 4u; ++channel) {
        loaded.scale[channel] = getFloat(record + 2u + 4u * channel);
        loaded.offsetMv[channel] = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(record[18u + 2u * channel] | record[19u + 2u * channel] << 8u));
        if (!plausibleScale(loaded.scale[channel])) return false;
    }
    calibration = loaded;
    return true;
}

bool AdInputIso12vCalibrationStore::save(std::uint8_t address, const AdInputIso12vCalibration& calibration) {
    std::size_t slot{};
    if (!slotFor(address, slot)) return false;
    slstm32::storage::RedundantBlobStore store{storage_, offset_ + static_cast<std::uint32_t>(slot * slotBytes), slotBytes};
    std::uint8_t record[recordBytes]{recordVersion, address};
    for (std::size_t channel = 0u; channel < 4u; ++channel) {
        putFloat(record + 2u + 4u * channel, calibration.scale[channel]);
        const auto offset = static_cast<std::uint16_t>(calibration.offsetMv[channel]);
        record[18u + 2u * channel] = static_cast<std::uint8_t>(offset);
        record[19u + 2u * channel] = static_cast<std::uint8_t>(offset >> 8u);
    }
    return store.commit(record, sizeof record);
}

// --- Calibrator -------------------------------------------------------------

bool AdInputIso12vCalibrator::registerCommands(slstm32::CommandConsole& console) {
    return console.add({"cal", "cal <addr> [<ch> <volts>|zero|reset | auto <volts>]  calibrate 12v-ad-input-iso", &calCommand, this}) &&
           console.add({"ain", "ain  readings of all 12v-ad-input-iso modules", &ainCommand, this});
}

bool AdInputIso12vCalibrator::init() {
    for (std::size_t index = 0u; index < moduleCount_; ++index) {
        auto& module = *modules_[index];
        AdInputIso12vCalibration calibration{};
        const bool stored = store_.load(module.adcAddress(), calibration);
        module.setCalibration(calibration);
        log_.writef(LogLevel::info, "cal 0x%02X: %s", static_cast<unsigned>(module.adcAddress()),
                    stored ? "stored calibration applied" : "not calibrated, design values");
    }
    return true;
}

AdInputIso12v* AdInputIso12vCalibrator::find(std::uint8_t address) const {
    for (std::size_t index = 0u; index < moduleCount_; ++index) {
        if (modules_[index]->adcAddress() == address) return modules_[index];
    }
    return nullptr;
}

bool AdInputIso12vCalibrator::begin(std::uint8_t address, std::size_t channel, Kind kind,
                                    std::int32_t referenceMv) {
    auto* module = find(address);
    if (!module) {
        log_.writef(LogLevel::warning, "cal: no module at 0x%02X", static_cast<unsigned>(address));
        return false;
    }
    if (active_) {
        log_.write(LogLevel::warning, "cal: a measurement is already running");
        return false;
    }
    if (!module->adcPresent() || channel >= AdInputIso12v::channels) {
        log_.writef(LogLevel::warning, "cal 0x%02X: module not answering or bad channel",
                    static_cast<unsigned>(address));
        return false;
    }
    session_ = {module, channel, kind, referenceMv, 0, 0u, module->analogSamples(channel), runtime_.millis()};
    active_ = true;
    log_.writef(LogLevel::info, "cal 0x%02X AI%u: measuring, keep the input steady",
                static_cast<unsigned>(address), static_cast<unsigned>(channel));
    return true;
}

bool AdInputIso12vCalibrator::startZero(std::uint8_t address, std::size_t channel) {
    return begin(address, channel, Kind::zero, 0);
}

bool AdInputIso12vCalibrator::startSpan(std::uint8_t address, std::size_t channel, std::int32_t referenceMv) {
    if (referenceMv < 1000) {
        log_.write(LogLevel::warning, "cal: use a reference of at least 1 V");
        return false;
    }
    return begin(address, channel, Kind::span, referenceMv);
}

bool AdInputIso12vCalibrator::reset(std::uint8_t address, std::size_t channel) {
    auto* module = find(address);
    if (!module || channel >= AdInputIso12v::channels) return false;
    auto calibration = module->calibration();
    calibration.scale[channel] = AdInputIso12vCalibration::designScale;
    calibration.offsetMv[channel] = 0;
    module->setCalibration(calibration);
    persist(*module);
    log_.writef(LogLevel::info, "cal 0x%02X AI%u: reset to design values", static_cast<unsigned>(address),
                static_cast<unsigned>(channel));
    return true;
}

void AdInputIso12vCalibrator::run() {
    if (autoActive_) runAuto();
    if (!active_) return;
    auto& module = *session_.module;
    const auto channel = session_.channel;
    if (static_cast<std::uint32_t>(runtime_.millis() - session_.startedAt) > sessionTimeoutMs ||
        !module.adcPresent()) {
        log_.writef(LogLevel::warning, "cal 0x%02X AI%u: no data, aborted",
                    static_cast<unsigned>(module.adcAddress()), static_cast<unsigned>(channel));
        active_ = false;
        return;
    }
    const auto samples = module.analogSamples(channel);
    if (samples == session_.lastSamples || !module.analogValid(channel)) return;
    session_.lastSamples = samples;
    session_.sumMicrovolts += module.adcMicrovolts(channel);
    if (++session_.count == averageSamples) finish();
}

void AdInputIso12vCalibrator::finish() {
    active_ = false;
    auto& module = *session_.module;
    const auto channel = session_.channel;
    const auto address = static_cast<unsigned>(module.adcAddress());
    const auto averageUv = static_cast<std::int32_t>(session_.sumMicrovolts / session_.count);
    auto calibration = module.calibration();
    char after[24];

    if (session_.kind == Kind::zero) {
        const auto offset = static_cast<std::int32_t>(static_cast<float>(averageUv) *
                                                      calibration.scale[channel] / 1000.0f);
        if (offset > maximumOffsetMv || offset < -maximumOffsetMv) {
            log_.writef(LogLevel::warning, "cal 0x%02X AI%u: offset %ld mV implausible, is the input shorted?",
                        address, static_cast<unsigned>(channel), static_cast<long>(offset));
            return;
        }
        log_.writef(LogLevel::info, "cal 0x%02X AI%u: offset %d -> %ld mV", address,
                    static_cast<unsigned>(channel), static_cast<int>(calibration.offsetMv[channel]),
                    static_cast<long>(offset));
        calibration.offsetMv[channel] = static_cast<std::int16_t>(offset);
    } else {
        if (averageUv < minimumSpanAdcMicrovolts) {
            log_.writef(LogLevel::warning, "cal 0x%02X AI%u: input too low, is the reference connected?",
                        address, static_cast<unsigned>(channel));
            return;
        }
        applySpan(module, channel, session_.referenceMv, averageUv);
        return;
    }
    module.setCalibration(calibration);
    persist(module);
    const auto terminalMv = static_cast<std::int32_t>(static_cast<float>(averageUv) *
                                                      calibration.scale[channel] / 1000.0f) -
                            calibration.offsetMv[channel];
    log_.writef(LogLevel::info, "cal 0x%02X AI%u: averaged input now reads %s V", address,
                static_cast<unsigned>(channel), fixed(after, terminalMv, 3u));
}

bool AdInputIso12vCalibrator::applySpan(AdInputIso12v& module, std::size_t channel, std::int32_t referenceMv,
                                        std::int32_t averageUv) {
    const auto address = static_cast<unsigned>(module.adcAddress());
    auto calibration = module.calibration();
    char before[24];
    char after[24];
    const float scale = static_cast<float>(referenceMv + calibration.offsetMv[channel]) * 1000.0f /
                        static_cast<float>(averageUv);
    if (!plausibleScale(scale)) {
        log_.writef(LogLevel::warning, "cal 0x%02X AI%u: scale %s implausible (design 9.2000)", address,
                    static_cast<unsigned>(channel), fixed(after, scaleTimes10000(scale), 4u));
        return false;
    }
    log_.writef(LogLevel::info, "cal 0x%02X AI%u: scale %s -> %s", address, static_cast<unsigned>(channel),
                fixed(before, scaleTimes10000(calibration.scale[channel]), 4u),
                fixed(after, scaleTimes10000(scale), 4u));
    calibration.scale[channel] = scale;
    module.setCalibration(calibration);
    persist(module);
    const auto terminalMv = static_cast<std::int32_t>(static_cast<float>(averageUv) * scale / 1000.0f) -
                            calibration.offsetMv[channel];
    log_.writef(LogLevel::info, "cal 0x%02X AI%u: averaged input now reads %s V", address,
                static_cast<unsigned>(channel), fixed(after, terminalMv, 3u));
    return true;
}

bool AdInputIso12vCalibrator::startAuto(std::uint8_t address, std::int32_t referenceMv) {
    auto* module = find(address);
    if (!module || !module->adcPresent()) {
        log_.writef(LogLevel::warning, "cal: no module answering at 0x%02X", static_cast<unsigned>(address));
        return false;
    }
    if (active_ || autoActive_) {
        log_.write(LogLevel::warning, "cal: a measurement is already running");
        return false;
    }
    if (referenceMv < 1000) {
        log_.write(LogLevel::warning, "cal: use a reference of at least 1 V");
        return false;
    }
    autoModule_ = module;
    autoReferenceMv_ = referenceMv;
    autoStartedAt_ = runtime_.millis();
    for (std::size_t channel = 0u; channel < AdInputIso12v::channels; ++channel) {
        autoChannels_[channel] = {};
        autoChannels_[channel].lastSamples = module->analogSamples(channel);
    }
    autoActive_ = true;
    char reference[24];
    log_.writef(LogLevel::info, "cal 0x%02X auto: touch each channel with %s V for 2 s, one after another",
                static_cast<unsigned>(address), fixed(reference, referenceMv, 3u));
    return true;
}

void AdInputIso12vCalibrator::runAuto() {
    auto& module = *autoModule_;
    const auto address = static_cast<unsigned>(module.adcAddress());
    const auto now = runtime_.millis();
    const auto low = autoReferenceMv_ * 4 / 5;
    const auto high = autoReferenceMv_ * 6 / 5;
    std::size_t done = 0u;
    for (std::size_t channel = 0u; channel < AdInputIso12v::channels; ++channel) {
        auto& state = autoChannels_[channel];
        if (state.phase == AutoPhase::done) {
            ++done;
            continue;
        }
        const auto samples = module.analogSamples(channel);
        if (samples == state.lastSamples || !module.analogValid(channel)) continue;
        state.lastSamples = samples;
        const auto millivolts = module.analogMillivolts(channel);
        const bool present = millivolts > low && millivolts < high;
        switch (state.phase) {
        case AutoPhase::waiting:
            if (present) {
                state.phase = AutoPhase::settling;
                state.since = now;
                char reading[24];
                log_.writef(LogLevel::info, "cal 0x%02X AI%u: voltage detected (%s V), hold the contact", address,
                            static_cast<unsigned>(channel), fixed(reading, millivolts, 3u));
                notify(channel, AutoEvent::detected);
            }
            break;
        case AutoPhase::settling:
            if (!present) {
                log_.writef(LogLevel::warning, "cal 0x%02X AI%u: contact lost, touch again", address,
                            static_cast<unsigned>(channel));
                state.phase = AutoPhase::waiting;
                notify(channel, AutoEvent::lost);
            } else if (static_cast<std::uint32_t>(now - state.since) >= autoSettleMs) {
                state.phase = AutoPhase::measuring;
                state.sumMicrovolts = 0;
                state.count = 0u;
            }
            break;
        case AutoPhase::measuring:
            if (!present) {
                log_.writef(LogLevel::warning, "cal 0x%02X AI%u: contact lost while measuring, touch again",
                            address, static_cast<unsigned>(channel));
                state.phase = AutoPhase::waiting;
                notify(channel, AutoEvent::lost);
                break;
            }
            state.sumMicrovolts += module.adcMicrovolts(channel);
            if (++state.count == autoSamples) {
                const auto averageUv = static_cast<std::int32_t>(state.sumMicrovolts / state.count);
                const bool applied = applySpan(module, channel, autoReferenceMv_, averageUv);
                state.phase = applied ? AutoPhase::done : AutoPhase::waiting;
                notify(channel, applied ? AutoEvent::done : AutoEvent::rejected);
            }
            break;
        case AutoPhase::done:
            break;
        }
    }
    if (done == AdInputIso12v::channels) {
        log_.writef(LogLevel::info, "cal 0x%02X auto: all channels calibrated", address);
        autoActive_ = false;
        notify(AdInputIso12v::channels, AutoEvent::finished);
    } else if (static_cast<std::uint32_t>(now - autoStartedAt_) > autoTimeoutMs) {
        log_.writef(LogLevel::warning, "cal 0x%02X auto: timeout, %u of 4 channels calibrated", address,
                    static_cast<unsigned>(done));
        autoActive_ = false;
        notify(AdInputIso12v::channels, AutoEvent::finished);
    }
}

void AdInputIso12vCalibrator::persist(AdInputIso12v& module) {
    if (!store_.save(module.adcAddress(), module.calibration())) {
        log_.writef(LogLevel::error, "cal 0x%02X: saving failed", static_cast<unsigned>(module.adcAddress()));
    }
}

void AdInputIso12vCalibrator::show(std::uint8_t address) const {
    const auto* module = find(address);
    if (!module) {
        log_.writef(LogLevel::warning, "cal: no module at 0x%02X", static_cast<unsigned>(address));
        return;
    }
    const auto& calibration = module->calibration();
    for (std::size_t channel = 0u; channel < AdInputIso12v::channels; ++channel) {
        char scale[24];
        log_.writef(LogLevel::info, "cal 0x%02X AI%u: scale %s, offset %d mV", static_cast<unsigned>(address),
                    static_cast<unsigned>(channel), fixed(scale, scaleTimes10000(calibration.scale[channel]), 4u),
                    static_cast<int>(calibration.offsetMv[channel]));
    }
}

void AdInputIso12vCalibrator::showReadings() const {
    for (std::size_t index = 0u; index < moduleCount_; ++index) {
        const auto& module = *modules_[index];
        const auto address = static_cast<unsigned>(module.adcAddress());
        if (!module.adcPresent()) {
            log_.writef(LogLevel::info, "ain 0x%02X: not answering", address);
            continue;
        }
        for (std::size_t channel = 0u; channel < AdInputIso12v::channels; ++channel) {
            char terminal[24];
            char adc[24];
            log_.writef(LogLevel::info, "ain 0x%02X AI%u: %s V (ADC %s mV)", address, static_cast<unsigned>(channel),
                        fixed(terminal, module.analogMillivolts(channel), 3u),
                        fixed(adc, module.adcMicrovolts(channel), 3u));
        }
    }
}

void AdInputIso12vCalibrator::calCommand(void* context, std::size_t argc, const char* const* argv) {
    auto& self = *static_cast<AdInputIso12vCalibrator*>(context);
    std::uint8_t address{};
    if (argc < 2u || !parseAddress(argv[1], address)) {
        self.log_.write(LogLevel::warning, "usage: cal <addr> [<ch> <volts>|zero|reset]");
        return;
    }
    if (argc == 2u) {
        self.show(address);
        return;
    }
    std::int32_t referenceMv{};
    if (argc == 4u && std::strcmp(argv[2], "auto") == 0) {
        if (parseMillivolts(argv[3], referenceMv)) {
            self.startAuto(address, referenceMv);
        } else {
            self.log_.writef(LogLevel::warning, "cal: '%s' is not a voltage", argv[3]);
        }
        return;
    }
    std::size_t channel{};
    if (argc != 4u || !parseChannel(argv[2], channel)) {
        self.log_.write(LogLevel::warning, "usage: cal <addr> <ch 0..3> <volts>|zero|reset");
        return;
    }
    if (std::strcmp(argv[3], "zero") == 0) {
        self.startZero(address, channel);
    } else if (std::strcmp(argv[3], "reset") == 0) {
        self.reset(address, channel);
    } else if (parseMillivolts(argv[3], referenceMv)) {
        self.startSpan(address, channel, referenceMv);
    } else {
        self.log_.writef(LogLevel::warning, "cal: '%s' is not a voltage, zero, or reset", argv[3]);
    }
}

void AdInputIso12vCalibrator::ainCommand(void* context, std::size_t, const char* const*) {
    static_cast<AdInputIso12vCalibrator*>(context)->showReadings();
}

} // namespace semantic_modules
