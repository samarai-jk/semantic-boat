#pragma once

#include <cstddef>
#include <cstdint>
#include "semantic_modules/ad_input_iso_12v.hpp"
#include "slstm32/console.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"
#include "slstm32/storage/byte_storage.hpp"

namespace semantic_modules {

// Persistent calibration of 12v-ad-input-iso modules, one power-loss-tolerant
// record per ADS1115 address (0x48..0x4B, so up to four modules per bus).
class AdInputIso12vCalibrationStore {
public:
    static constexpr std::size_t slotBytes = 128u;
    static constexpr std::size_t slots = 4u;
    static constexpr std::size_t requiredBytes = slots * slotBytes;

    AdInputIso12vCalibrationStore(slstm32::storage::ByteStorage& storage, std::uint32_t offset)
        : storage_(storage), offset_(offset) {}

    // Returns false (and leaves `calibration` untouched) without a valid record.
    bool load(std::uint8_t address, AdInputIso12vCalibration& calibration);
    bool save(std::uint8_t address, const AdInputIso12vCalibration& calibration);

private:
    slstm32::storage::ByteStorage& storage_;
    std::uint32_t offset_{};
};

// Applies stored calibrations at startup and calibrates channels on request.
//
// Console commands (see registerCommands()):
//   ain                              terminal and ADC values of all modules
//   cal <addr>                       show the calibration of a module
//   cal <addr> <ch> zero             terminal shorted to GND_I: measure the offset
//   cal <addr> <ch> <volts>          reference applied (for example 11.972):
//                                    measure the scale factor
//   cal <addr> <ch> reset            back to the design values
//   cal <addr> auto <volts>          span calibration of every channel that
//                                    gets touched with the reference, one
//                                    after another, within ten minutes
// A measurement averages 32 samples (about 1.2 s), is checked for
// plausibility (scale within 5 % of the design value, offset below 100 mV),
// applied at once, and saved. Calibrate zero first, then the span.
class AdInputIso12vCalibrator final : public slstm32::Service {
public:
    AdInputIso12vCalibrator(slstm32::Runtime runtime, slstm32::Logger log,
                            AdInputIso12vCalibrationStore& store, AdInputIso12v* const* modules,
                            std::size_t moduleCount)
        : runtime_(runtime), log_(log), store_(store), modules_(modules), moduleCount_(moduleCount) {}

    bool registerCommands(slstm32::CommandConsole& console);
    bool init() override;
    void run() override;

    bool startZero(std::uint8_t address, std::size_t channel);
    bool startSpan(std::uint8_t address, std::size_t channel, std::int32_t referenceMv);
    bool reset(std::uint8_t address, std::size_t channel);
    // Calibrates each channel as soon as the reference appears on it: the
    // reading must stay within 80..120 % of the reference for 0.3 s to settle
    // and then for 24 samples (about 0.9 s) that are averaged. A broken
    // contact discards the measurement; the channel waits for the next touch.
    bool startAuto(std::uint8_t address, std::int32_t referenceMv);

    // Optional progress events of the automatic calibration, for feedback on
    // the device itself (LEDs, sounds) while the operator works at the bench.
    enum class AutoEvent : std::uint8_t {
        detected,  // reference seen on the channel, measurement starts
        lost,      // reference gone before the measurement completed
        done,      // channel calibrated and saved
        rejected,  // measurement implausible, channel waits for a new touch
        finished,  // session ended (all channels done or timeout)
    };
    using AutoFeedback = void (*)(void* context, std::size_t channel, AutoEvent event);
    void onAutoEvent(AutoFeedback feedback, void* context) {
        autoFeedback_ = feedback;
        autoFeedbackContext_ = context;
    }
    void show(std::uint8_t address) const;
    void showReadings() const;

private:
    enum class Kind : std::uint8_t { zero, span };
    struct Session {
        AdInputIso12v* module{};
        std::size_t channel{};
        Kind kind{Kind::zero};
        std::int32_t referenceMv{};
        std::int64_t sumMicrovolts{};
        std::uint32_t count{};
        std::uint32_t lastSamples{};
        std::uint32_t startedAt{};
    };

    static void calCommand(void* context, std::size_t argc, const char* const* argv);
    static void ainCommand(void* context, std::size_t argc, const char* const* argv);
    AdInputIso12v* find(std::uint8_t address) const;
    bool begin(std::uint8_t address, std::size_t channel, Kind kind, std::int32_t referenceMv);
    void finish();
    void persist(AdInputIso12v& module);
    void runAuto();
    void notify(std::size_t channel, AutoEvent event) {
        if (autoFeedback_) autoFeedback_(autoFeedbackContext_, channel, event);
    }
    bool applySpan(AdInputIso12v& module, std::size_t channel, std::int32_t referenceMv,
                   std::int32_t averageUv);

    enum class AutoPhase : std::uint8_t { waiting, settling, measuring, done };
    struct AutoChannel {
        AutoPhase phase{AutoPhase::waiting};
        std::uint32_t since{};
        std::uint32_t lastSamples{};
        std::int64_t sumMicrovolts{};
        std::uint32_t count{};
    };

    slstm32::Runtime runtime_;
    slstm32::Logger log_;
    AdInputIso12vCalibrationStore& store_;
    AdInputIso12v* const* modules_{};
    std::size_t moduleCount_{};
    Session session_{};
    bool active_{};
    AdInputIso12v* autoModule_{};
    std::int32_t autoReferenceMv_{};
    std::uint32_t autoStartedAt_{};
    AutoChannel autoChannels_[AdInputIso12v::channels]{};
    bool autoActive_{};
    AutoFeedback autoFeedback_{};
    void* autoFeedbackContext_{};
};

} // namespace semantic_modules
