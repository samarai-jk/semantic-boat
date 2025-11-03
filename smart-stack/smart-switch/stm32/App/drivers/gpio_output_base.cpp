#include "drivers/gpio_output_base.hpp"

namespace app
{

    void GpioOutputDriverBase::init()
    {
        GPIO_PinState s = HAL_GPIO_ReadPin(gpioPort(), gpioPin());
        state_ = (s == GPIO_PIN_SET);
    }

    void GpioOutputDriverBase::set(bool high)
    {
        state_ = high;
        HAL_GPIO_WritePin(gpioPort(), gpioPin(), high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

} // namespace app
