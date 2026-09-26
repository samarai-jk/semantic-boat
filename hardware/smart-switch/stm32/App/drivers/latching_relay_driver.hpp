#ifndef APP_DRIVERS_LATCHING_RELAY_DRIVER_HPP
#define APP_DRIVERS_LATCHING_RELAY_DRIVER_HPP

#include <cstdint>
#include "driver.hpp"

extern "C"
{
#include "main.h"
}

namespace app
{

    // Non-blocking latching relay driver controlling GPIOA pins PA0 (Set) and PA1 (Reset).
    // Only one output is ever active at a time; pulses are automatically ended after pulse_ms.
    class LatchingRelayDriver : public IDriver
    {
    public:
        explicit LatchingRelayDriver(uint32_t pulse_ms = 100);
        ~LatchingRelayDriver() override = default;

        void init() override;
        void run() override;

        // Schedule a non-blocking pulse on PA0 to switch the relay ON
        void switchOn();
        // Schedule a non-blocking pulse on PA1 to switch the relay OFF
        void switchOff();

        void setPulseMs(uint32_t ms) { pulse_ms_ = ms; }
        uint32_t pulseMs() const { return pulse_ms_; }

        bool isBusy() const { return state_ != State::Idle; }

    private:
        enum class State : uint8_t { Idle, PulsingOn, PulsingOff };
        enum class Pending : uint8_t { None, On, Off };

        void startOnPulse();
        void startOffPulse();
        void endPulse();
        static bool deadlineExpired(uint32_t deadline, uint32_t now);

        State state_{State::Idle};
        Pending pending_{Pending::None};
        uint32_t pulse_ms_{100};
        uint32_t deadline_ms_{0};
    };

} // namespace app

#endif // APP_DRIVERS_LATCHING_RELAY_DRIVER_HPP
