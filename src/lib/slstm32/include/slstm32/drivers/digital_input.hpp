#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/hardware.hpp"
#include "slstm32/runtime.hpp"

namespace slstm32::drivers {

// Polled logical digital input. Without a debounce time the value follows
// the pin on every run(). With a debounce time (requires a runtime clock) a
// new level is accepted only after it has been stable for that long.
// changes() counts accepted transitions.
class DigitalInputDriver : public Driver {
public:
    explicit DigitalInputDriver(DigitalInputHardware hardware) : hardware_(hardware) {}
    DigitalInputDriver(Runtime runtime, DigitalInputHardware hardware, std::uint32_t debounceMs)
        : runtime_(runtime), hardware_(hardware), debounceMs_(debounceMs) {}

    bool init() override;
    void run() override;
    bool value() const { return value_; }
    std::uint32_t changes() const { return changes_; }

private:
    bool readLogical() const;

    Runtime runtime_{};
    DigitalInputHardware hardware_;
    std::uint32_t debounceMs_{};
    std::uint32_t candidateSince_{};
    std::uint32_t changes_{};
    bool value_{};
    bool candidate_{};
};

} // namespace slstm32::drivers
