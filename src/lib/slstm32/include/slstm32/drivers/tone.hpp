#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/hardware.hpp"
#include "slstm32/runtime.hpp"

namespace slstm32::drivers {

class ToneDriver final : public Driver {
public:
    ToneDriver(Runtime runtime, ToneOutputHardware hardware)
        : runtime_(runtime), hardware_(hardware) {}
    bool init() override;
    void run() override;
    // Optional deadline service for a periodic interrupt. Use this when a
    // foreground operation may take longer than a short tone; the hardware
    // stop callback must be safe to call from that interrupt.
    void onTick();
    bool play(std::uint32_t frequencyHz, std::uint32_t durationMs, float level = 1.0f);
    void stop();
    bool playing() const { return playing_; }

private:
    Runtime runtime_;
    ToneOutputHardware hardware_;
    volatile bool playing_{};
    volatile std::uint32_t stopAt_{};
};

} // namespace slstm32::drivers
