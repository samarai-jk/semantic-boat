#include "drivers/latching_relay_driver.hpp"

namespace app
{

static inline void bothLow()
{
    HAL_GPIO_WritePin(RELAY_ON_GPIO_Port, RELAY_ON_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RELAY_OFF_GPIO_Port, RELAY_OFF_Pin, GPIO_PIN_RESET);
}

static inline void driveOnPulse()
{
    // Only PA0 high
    HAL_GPIO_WritePin(RELAY_ON_GPIO_Port, RELAY_ON_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RELAY_OFF_GPIO_Port, RELAY_OFF_Pin, GPIO_PIN_SET);
}

static inline void driveOffPulse()
{
    // Only PA1 high
    HAL_GPIO_WritePin(RELAY_ON_GPIO_Port, RELAY_ON_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(RELAY_OFF_GPIO_Port, RELAY_OFF_Pin, GPIO_PIN_RESET);
}

bool LatchingRelayDriver::deadlineExpired(uint32_t deadline, uint32_t now)
{
    // Wrap-safe compare
    return static_cast<int32_t>(deadline - now) <= 0;
}

LatchingRelayDriver::LatchingRelayDriver(uint32_t pulse_ms)
    : pulse_ms_(pulse_ms)
{
}

void LatchingRelayDriver::init()
{
    // Ensure both outputs are low at startup
    bothLow();
    state_ = State::Idle;
    pending_ = Pending::None;
    deadline_ms_ = 0;
}

void LatchingRelayDriver::run()
{
    const uint32_t now = HAL_GetTick();

    switch (state_)
    {
    case State::Idle:
        // If there is a pending action schedule it now
        if (pending_ == Pending::On)
        {
            pending_ = Pending::None;
            startOnPulse();
        }
        else if (pending_ == Pending::Off)
        {
            pending_ = Pending::None;
            startOffPulse();
        }
        break;

    case State::PulsingOn:
    case State::PulsingOff:
        if (deadlineExpired(deadline_ms_, now))
        {
            endPulse();
            // Execute any pending request immediately after finishing the current pulse
            if (pending_ == Pending::On)
            {
                pending_ = Pending::None;
                startOnPulse();
            }
            else if (pending_ == Pending::Off)
            {
                pending_ = Pending::None;
                startOffPulse();
            }
        }
        break;
    }
}

void LatchingRelayDriver::switchOn()
{
    if (state_ == State::Idle)
    {
        startOnPulse();
    }
    else
    {
        // Queue latest request
        pending_ = Pending::On;
    }
}

void LatchingRelayDriver::switchOff()
{
    if (state_ == State::Idle)
    {
        startOffPulse();
    }
    else
    {
        // Queue latest request
        pending_ = Pending::Off;
    }
}

void LatchingRelayDriver::startOnPulse()
{
    driveOnPulse();
    state_ = State::PulsingOn;
    deadline_ms_ = HAL_GetTick() + pulse_ms_;
}

void LatchingRelayDriver::startOffPulse()
{
    driveOffPulse();
    state_ = State::PulsingOff;
    deadline_ms_ = HAL_GetTick() + pulse_ms_;
}

void LatchingRelayDriver::endPulse()
{
    bothLow();
    state_ = State::Idle;
    deadline_ms_ = 0;
}

} // namespace app
