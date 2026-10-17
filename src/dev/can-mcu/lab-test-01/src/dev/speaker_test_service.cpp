// DEVELOPMENT AND TEST CODE: compiled only with SB_DEV_TOOLS (AGENTS.md,
// "Firmware code categories and build profiles"). Never reference this from
// product code.
#include "speaker_test_service.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "slstm32/drivers/button.hpp"
extern "C" {
#include "main.h"
}

namespace lab_test_01 {

namespace {

using slstm32::LogLevel;

constexpr std::uint32_t sampleRate = 16000u;
constexpr std::uint32_t timerClock = 80000000u; // APB1 = HCLK = 80 MHz
constexpr std::size_t oscillatorCount = 3u;

enum Wave : std::uint8_t { sine, square, triangle };
const char* const waveNames[]{"sine", "square", "triangle"};

// Written by the main loop, read by the sample interrupt.
struct Oscillator {
    volatile std::uint32_t step{};  // phase increment per sample (2^32 = one period)
    volatile std::uint16_t level{}; // 0..32767
    volatile std::uint8_t wave{};
    std::uint32_t phase{};
};
Oscillator oscillators[oscillatorCount];
volatile std::uint16_t masterVolume{};  // 0..32767
std::int16_t sineTable[256];

constexpr std::uint16_t volumeSteps[]{328, 655, 1311, 2294, 3277, 4915, 8192, 13107, 19661, 32767}; // 1..100 %
constexpr std::uint8_t volumePercent[]{1, 2, 4, 7, 10, 15, 25, 40, 60, 100};

std::uint32_t stepFor(std::uint32_t hz) {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(hz) << 32u) / sampleRate);
}

void set(std::size_t index, std::uint32_t hz, std::uint16_t level, std::uint8_t wave = sine) {
    auto& oscillator = oscillators[index];
    oscillator.step = stepFor(hz);
    oscillator.wave = wave;
    oscillator.level = level;
}

void silence() {
    for (auto& oscillator : oscillators) oscillator.level = 0u;
}

void startHardware() {
    for (std::size_t index = 0u; index < 256u; ++index) {
        sineTable[index] = static_cast<std::int16_t>(std::lround(32767.0 * std::sin(index * 6.283185307179586 / 256.0)));
    }
    // PA4 analog, DAC channel 1 with output buffer, mid level.
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIOA->MODER |= 3u << (4u * 2u);
    GPIOA->PUPDR &= ~(3u << (4u * 2u));
    RCC->APB1ENR1 |= RCC_APB1ENR1_DAC1EN;
    (void)RCC->APB1ENR1;
    DAC1->MCR &= ~DAC_MCR_MODE1;
    DAC1->CR |= DAC_CR_EN1;
    DAC1->DHR12R1 = 2048u;
    // TIM6 update interrupt at the sample rate.
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;
    (void)RCC->APB1ENR1;
    TIM6->PSC = 0u;
    TIM6->ARR = timerClock / sampleRate - 1u;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0u;
    TIM6->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 6u, 0u);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    TIM6->CR1 = TIM_CR1_CEN;
}

std::int32_t waveSample(Oscillator& oscillator) {
    oscillator.phase += oscillator.step;
    const auto phase = oscillator.phase;
    switch (oscillator.wave) {
    case square: return phase & 0x80000000u ? -32767 : 32767;
    case triangle: {
        const auto ramp = static_cast<std::int32_t>(phase >> 16u); // 0..65535
        return ramp < 32768 ? ramp * 2 - 32767 : (65535 - ramp) * 2 - 32767;
    }
    default: return sineTable[phase >> 24u];
    }
}

} // namespace

extern "C" void TIM6_DAC_IRQHandler(void) {
    TIM6->SR = 0u;
    std::int32_t mix = 0;
    for (auto& oscillator : oscillators) {
        if (oscillator.level != 0u) mix += waveSample(oscillator) * oscillator.level >> 15;
    }
    if (mix > 32767) mix = 32767;
    if (mix < -32767) mix = -32767;
    mix = mix * masterVolume >> 15;
    // 12-bit DAC around mid scale; clip at the rails.
    std::int32_t value = 2048 + (mix >> 4);
    if (value < 0) value = 0;
    if (value > 4095) value = 4095;
    DAC1->DHR12R1 = static_cast<std::uint32_t>(value);
}

bool SpeakerTestService::init() {
    startHardware();
    setVolume(volumeStep_);
    const bool ok = events_.subscribe(buttonEvent_, &onButton, this) &&
                    console_.add({"speaker",
                                  "speaker [stop | tone <Hz> <ms> [sine|square|triangle] | volume <1-10>]  "
                                  "speaker test (dev, temporary)",
                                  &command, this});
    log_.write(LogLevel::warning,
               "speaker test on PA4 (DAC, 16 kHz): BF1/BF2 volume, BF3 sweep, BF4 waveforms, BF5 alarms, BF6 chimes");
    return ok;
}

void SpeakerTestService::setVolume(std::uint8_t step) {
    volumeStep_ = step > 9u ? 9u : step;
    masterVolume = volumeSteps[volumeStep_];
    log_.writef(LogLevel::info, "speaker: volume %u %% (step %u of 10)", static_cast<unsigned>(volumePercent[volumeStep_]),
                static_cast<unsigned>(volumeStep_ + 1u));
}

void SpeakerTestService::onButton(slstm32::EventId, const void* payload, std::uint8_t size, void* context) {
    if (size != sizeof(slstm32::drivers::ButtonEvent)) return;
    const auto& event = *static_cast<const slstm32::drivers::ButtonEvent*>(payload);
    if (event.pressed) static_cast<SpeakerTestService*>(context)->pressed(event.id);
}

void SpeakerTestService::pressed(std::uint8_t id) {
    const auto now = runtime_.millis();
    switch (id) {
    case 0u:
        if (volumeStep_ > 0u) setVolume(static_cast<std::uint8_t>(volumeStep_ - 1u));
        break;
    case 1u:
        setVolume(static_cast<std::uint8_t>(volumeStep_ + 1u));
        break;
    case 2u:
        if (program_ == Program::sweep) {
            stop();
        } else {
            start(Program::sweep, 0u, now);
        }
        break;
    case 3u:
        start(Program::waveform, waveformVariant_, now);
        waveformVariant_ = static_cast<std::uint8_t>((waveformVariant_ + 1u) % 9u);
        break;
    case 4u:
        start(Program::alarm, alarmVariant_, now);
        alarmVariant_ = static_cast<std::uint8_t>((alarmVariant_ + 1u) % 6u);
        break;
    case 5u:
        start(Program::chime, chimeVariant_, now);
        chimeVariant_ = static_cast<std::uint8_t>((chimeVariant_ + 1u) % 3u);
        break;
    default:
        break;
    }
}

void SpeakerTestService::stop() {
    silence();
    if (program_ != Program::none) log_.write(LogLevel::info, "speaker: stopped");
    program_ = Program::none;
}

void SpeakerTestService::start(Program program, std::uint8_t variant, std::uint32_t now) {
    silence();
    program_ = program;
    variant_ = variant;
    startedAt_ = now;
    sweepStep_ = -1;
    static const char* const alarmNames[]{"wail siren", "yelp siren", "two-tone hi-lo 960/770 Hz",
                                          "beep series 2 kHz", "general alarm 7 short + 1 long",
                                          "'have a look' chirp"};
    static const char* const chimeNames[]{"ding-dong bell", "three-note chime", "single bell strike"};
    switch (program) {
    case Program::waveform: {
        static constexpr std::uint16_t hz[]{500u, 1000u, 3000u};
        log_.writef(LogLevel::info, "speaker: %s %u Hz, 4 s", waveNames[variant % 3u],
                    static_cast<unsigned>(hz[variant / 3u]));
        break;
    }
    case Program::alarm:
        log_.writef(LogLevel::info, "speaker: %s", alarmNames[variant]);
        break;
    case Program::chime:
        log_.writef(LogLevel::info, "speaker: %s", chimeNames[variant]);
        break;
    case Program::tone:
        log_.writef(LogLevel::info, "speaker: %s %lu Hz, %lu ms", waveNames[toneWave_],
                    static_cast<unsigned long>(toneHz_), static_cast<unsigned long>(toneMs_));
        break;
    default:
        break;
    }
}

void SpeakerTestService::run() {
    const auto now = runtime_.millis();
    if (program_ == Program::none || now == lastUpdate_) return;
    lastUpdate_ = now;
    update(now - startedAt_);
}

// Sets the oscillators for the time since the program started (called every ms).
void SpeakerTestService::update(std::uint32_t t) {
    constexpr std::uint16_t full = 32767u;
    switch (program_) {
    case Program::tone:
        if (t >= toneMs_) {
            stop();
            return;
        }
        set(0, toneHz_, full, toneWave_);
        return;
    case Program::sweep: {
        // Sine steps for loudness by frequency, 2 s each.
        static constexpr std::uint16_t hz[]{200u,  300u,  400u,  500u,  700u,  1000u, 1500u, 2000u,
                                            2500u, 3000u, 4000u, 5000u, 6000u, 8000u};
        const auto step = static_cast<std::int32_t>(t / 2000u);
        if (step >= static_cast<std::int32_t>(sizeof hz / sizeof hz[0])) {
            stop();
            return;
        }
        if (step != sweepStep_) {
            sweepStep_ = step;
            set(0, hz[step], full);
            log_.writef(LogLevel::info, "speaker: sweep %u Hz", static_cast<unsigned>(hz[step]));
        }
        return;
    }
    case Program::waveform: {
        static constexpr std::uint16_t hz[]{500u, 1000u, 3000u};
        if (t >= 4000u) {
            stop();
            return;
        }
        set(0, hz[variant_ / 3u], full, static_cast<std::uint8_t>(variant_ % 3u));
        return;
    }
    case Program::alarm:
        switch (variant_) {
        case 0: { // wail: 600..1300 Hz up and down over 4 s, 3 cycles
            if (t >= 12000u) break;
            const float x = 0.5f - 0.5f * std::cos(6.2831853f * static_cast<float>(t % 4000u) / 4000.0f);
            set(0, 600u + static_cast<std::uint32_t>(700.0f * x), full);
            return;
        }
        case 1: { // yelp: 600..1300 Hz rising sawtooth, 4 per second, 6 s
            if (t >= 6000u) break;
            set(0, 600u + (t % 250u) * 700u / 250u, full);
            return;
        }
        case 2: // hi-lo: 960 / 770 Hz, 0.5 s each, 6 s
            if (t >= 6000u) break;
            set(0, (t / 500u) % 2u ? 770u : 960u, full);
            return;
        case 3: { // beep series: 3 x 150 ms at 2 kHz, repeated 3 times every 2 s
            if (t >= 6000u) break;
            const auto inCycle = t % 2000u;
            const bool on = inCycle < 900u && (inCycle % 300u) < 150u;
            set(0, 2000u, on ? full : 0u, square);
            return;
        }
        case 4: { // general alarm: 7 short (300 ms) + 1 long (1.5 s), 1 kHz
            if (t >= 7u * 600u + 1500u) break;
            const bool on = t < 7u * 600u ? (t % 600u) < 300u : true;
            set(0, 1000u, on ? full : 0u, square);
            return;
        }
        case 5: { // "have a look": two quiet up-chirps 1.5..3 kHz, 60 ms
            if (t >= 300u) break;
            const auto inChirp = t % 150u;
            const bool on = inChirp < 60u;
            set(0, 1500u + inChirp * 1500u / 60u, on ? static_cast<std::uint16_t>(full / 4u) : 0u);
            return;
        }
        default:
            break;
        }
        stop();
        return;
    case Program::chime: {
        // Bell-like strikes: a fundamental plus two inharmonic partials, each
        // decaying exponentially.
        struct Strike {
            std::uint16_t at;
            std::uint16_t hz;
        };
        static constexpr Strike dingDong[]{{0u, 659u}, {700u, 523u}};
        static constexpr Strike threeNote[]{{0u, 523u}, {350u, 659u}, {700u, 784u}};
        static constexpr Strike single[]{{0u, 880u}};
        const Strike* strikes = variant_ == 0u ? dingDong : variant_ == 1u ? threeNote : single;
        const std::size_t count = variant_ == 0u ? 2u : variant_ == 1u ? 3u : 1u;
        if (t >= strikes[count - 1u].at + 2500u) {
            stop();
            return;
        }
        // The most recent strike sounds; earlier ones are cut (one voice of three partials).
        std::size_t current = 0u;
        for (std::size_t index = 0u; index < count; ++index) {
            if (t >= strikes[index].at) current = index;
        }
        const float since = static_cast<float>(t - strikes[current].at) / 1000.0f;
        const auto hz = strikes[current].hz;
        const auto level = [](float seconds, float decay, float share) {
            return static_cast<std::uint16_t>(32767.0f * share * std::exp(-seconds / decay));
        };
        set(0, hz, level(since, 0.8f, 0.6f));
        set(1, static_cast<std::uint32_t>(hz * 2.76f), level(since, 0.3f, 0.25f));
        set(2, static_cast<std::uint32_t>(hz * 5.40f), level(since, 0.12f, 0.15f));
        return;
    }
    default:
        return;
    }
}

void SpeakerTestService::command(void* context, std::size_t argc, const char* const* argv) {
    auto& self = *static_cast<SpeakerTestService*>(context);
    if (argc == 2u && std::strcmp(argv[1], "stop") == 0) {
        self.stop();
        return;
    }
    if (argc == 3u && std::strcmp(argv[1], "volume") == 0) {
        const auto step = std::strtoul(argv[2], nullptr, 10);
        if (step >= 1u && step <= 10u) {
            self.setVolume(static_cast<std::uint8_t>(step - 1u));
            return;
        }
    }
    if ((argc == 4u || argc == 5u) && std::strcmp(argv[1], "tone") == 0) {
        self.toneHz_ = std::strtoul(argv[2], nullptr, 10);
        self.toneMs_ = std::strtoul(argv[3], nullptr, 10);
        self.toneWave_ = sine;
        if (argc == 5u) {
            for (std::uint8_t wave = 0u; wave < 3u; ++wave) {
                if (std::strcmp(argv[4], waveNames[wave]) == 0) self.toneWave_ = wave;
            }
        }
        if (self.toneHz_ >= 20u && self.toneHz_ <= 7900u && self.toneMs_ > 0u) {
            self.start(Program::tone, 0u, self.runtime_.millis());
            return;
        }
    }
    self.log_.write(LogLevel::warning, "usage: speaker [stop | tone <Hz> <ms> [sine|square|triangle] | volume <1-10>]");
}

} // namespace lab_test_01
