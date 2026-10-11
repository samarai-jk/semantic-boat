#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/hardware.hpp"
#include "slstm32/runtime.hpp"

namespace slstm32::drivers {

class LedDriver final : public Driver {
public:
    LedDriver(Runtime runtime, DigitalOutputHardware hardware, bool initialValue = false)
        : runtime_(runtime), hardware_(hardware), value_(initialValue) {}

    bool init() override;
    void run() override;
    void set(bool value);
    void on() { set(true); }
    void off() { set(false); }
    void toggle() { set(!value_); }
    void pulse(std::uint32_t durationMs);
    void blink(std::uint32_t periodMs, std::uint32_t onTimeMs);
    bool value() const { return value_; }

private:
    enum class Mode : std::uint8_t { steady, pulse, blink };
    void write(bool value);

    Runtime runtime_;
    DigitalOutputHardware hardware_;
    Mode mode_{Mode::steady};
    bool value_{};
    std::uint32_t deadline_{};
    std::uint32_t periodMs_{};
    std::uint32_t onTimeMs_{};
};

} // namespace slstm32::drivers
