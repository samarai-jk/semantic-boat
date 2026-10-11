#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/hardware.hpp"
#include "slstm32/runtime.hpp"

namespace slstm32::drivers {

struct AnalogInputConfig {
    AnalogInputHardware hardware{};
    std::uint16_t averageWindow{1u};
    std::uint32_t samplePeriodMs{};
    float offset{};
    float scale{1.0f};
};

class AnalogInputDriver final : public Driver {
public:
    static constexpr std::size_t maximumAverageWindow = 128u;

    AnalogInputDriver(Runtime runtime, AnalogInputConfig config)
        : runtime_(runtime), config_(config) {}
    bool init() override;
    void run() override;
    void setEnabled(bool enabled) override;
    bool sample();
    bool setAveragingWindow(std::uint16_t samples);
    std::uint16_t averagingWindow() const { return config_.averageWindow; }
    std::int32_t raw() const { return raw_; }
    float value() const { return value_; }
    bool valid() const { return valid_; }

private:
    void resetAverage();

    Runtime runtime_;
    AnalogInputConfig config_;
    std::array<std::int32_t, maximumAverageWindow> samples_{};
    std::size_t sampleIndex_{};
    std::size_t sampleCount_{};
    std::int64_t sampleSum_{};
    std::int32_t raw_{};
    float value_{};
    std::uint32_t nextSampleAt_{};
    bool valid_{};
};

} // namespace slstm32::drivers
