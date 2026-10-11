#pragma once

#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/hardware.hpp"
#include "slstm32/runtime.hpp"

namespace slstm32::drivers {

class LatchingRelayDriver final : public Driver {
public:
    LatchingRelayDriver(Runtime runtime, DigitalOutputHardware setCoil,
                        DigitalOutputHardware resetCoil, std::uint32_t pulseMs = 20u,
                        bool initialState = false)
        : runtime_(runtime), setCoil_(setCoil), resetCoil_(resetCoil), pulseMs_(pulseMs),
          state_(initialState), requestedState_(initialState) {}

    bool init() override;
    void run() override;
    void set(bool state);
    bool state() const { return state_; }
    bool busy() const { return pulsing_; }

private:
    void write(const DigitalOutputHardware& output, bool value);

    Runtime runtime_;
    DigitalOutputHardware setCoil_;
    DigitalOutputHardware resetCoil_;
    std::uint32_t pulseMs_{};
    bool state_{};
    bool requestedState_{};
    bool pulseState_{};
    bool requestPending_{};
    bool pulsing_{};
    std::uint32_t pulseEndsAt_{};
};

} // namespace slstm32::drivers
