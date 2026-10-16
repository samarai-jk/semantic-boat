#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/runtime.hpp"

namespace slstm32::drivers {

// TI ADS1115 16-bit ADC on an I2cBus. Configured channels are converted in
// single-shot mode one after the other, without blocking: run() starts a
// conversion, returns, and reads the result once the conversion time has
// passed. With 128 samples/s, four channels refresh about every 32 ms.
//
// A device that does not answer does not fail init(): its channels report
// !valid() and the driver retries once per second, so a missing or unplugged
// module never stops the application.
class Ads1115Driver final : public Driver {
public:
    static constexpr std::uint8_t baseAddress = 0x48u; // ADDR to GND; 0x49 VDD, 0x4A SDA, 0x4B SCL
    static constexpr std::size_t maximumChannels = 8u;

    // Full-scale range of the programmable gain amplifier.
    enum class Gain : std::uint8_t { fsr6144, fsr4096, fsr2048, fsr1024, fsr512, fsr256 };
    enum class DataRate : std::uint8_t { sps8, sps16, sps32, sps64, sps128, sps250, sps475, sps860 };
    // Multiplexer setting: differential pairs or single-ended against GND.
    enum class Input : std::uint8_t {
        ain0MinusAin1, ain0MinusAin3, ain1MinusAin3, ain2MinusAin3, ain0, ain1, ain2, ain3,
    };

    struct Channel {
        Input input{Input::ain0};
        Gain gain{Gain::fsr2048};
    };

    Ads1115Driver(Runtime runtime, I2cBus& bus, std::uint8_t address, const Channel* channels,
                  std::size_t channelCount, DataRate dataRate = DataRate::sps128)
        : runtime_(runtime), bus_(bus), address_(address), channels_(channels),
          channelCount_(channelCount), dataRate_(dataRate) {}

    bool init() override;
    void run() override;

    bool present() const { return present_; }
    std::uint8_t address() const { return address_; }
    bool valid(std::size_t channel) const { return channel < channelCount_ && valid_[channel]; }
    std::int16_t raw(std::size_t channel) const { return channel < channelCount_ ? raw_[channel] : 0; }
    // Voltage at the ADC input of the channel (differential or against GND).
    std::int32_t microvolts(std::size_t channel) const;
    // Increments with every new result of the channel.
    std::uint32_t samples(std::size_t channel) const {
        return channel < channelCount_ ? samples_[channel] : 0u;
    }
    std::uint32_t errors() const { return errors_; }

    static std::int32_t fullScaleMicrovolts(Gain gain);
    static std::uint32_t conversionMicroseconds(DataRate rate);

private:
    enum class Phase : std::uint8_t { idle, converting };

    bool startConversion(std::uint32_t now);
    void fail(std::uint32_t now);

    Runtime runtime_;
    I2cBus& bus_;
    std::uint8_t address_{};
    const Channel* channels_{};
    std::size_t channelCount_{};
    DataRate dataRate_{};
    Phase phase_{Phase::idle};
    std::size_t current_{};
    std::uint32_t startedAt_{};
    std::uint32_t retryAt_{};
    std::array<std::int16_t, maximumChannels> raw_{};
    std::array<std::uint32_t, maximumChannels> samples_{};
    std::array<bool, maximumChannels> valid_{};
    std::uint32_t errors_{};
    bool present_{};
};

} // namespace slstm32::drivers
