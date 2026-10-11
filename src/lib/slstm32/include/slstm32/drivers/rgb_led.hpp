#pragma once

#include <array>
#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/hardware.hpp"
#include "slstm32/runtime.hpp"

namespace slstm32::drivers {

struct RgbColor {
    float red{};
    float green{};
    float blue{};
};

class RgbLedDriver final : public Driver {
public:
    RgbLedDriver(Runtime runtime, std::array<PwmOutputHardware, 3> channels,
                 bool commonAnode = false)
        : runtime_(runtime), channels_(channels), commonAnode_(commonAnode) {}

    bool init() override;
    void run() override;
    void setColor(RgbColor color);
    void off() { setColor({}); }
    void blink(RgbColor color, std::uint32_t periodMs, std::uint32_t onTimeMs);
    RgbColor color() const { return color_; }

private:
    void write(RgbColor color);
    static float clamp(float value);

    Runtime runtime_;
    std::array<PwmOutputHardware, 3> channels_;
    RgbColor color_{};
    RgbColor blinkColor_{};
    bool commonAnode_{};
    bool blinking_{};
    bool outputOn_{};
    std::uint32_t startedAt_{};
    std::uint32_t periodMs_{};
    std::uint32_t onTimeMs_{};
};

} // namespace slstm32::drivers
