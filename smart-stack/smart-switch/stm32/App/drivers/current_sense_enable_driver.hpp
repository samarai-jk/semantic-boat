#ifndef APP_DRIVERS_CURRENT_SENSE_ENABLE_DRIVER_HPP
#define APP_DRIVERS_CURRENT_SENSE_ENABLE_DRIVER_HPP

#include "drivers/gpio_output_base.hpp"

namespace app
{

    // Enables current sensing rail via PA12 (CUR_SENSE_ON)
    class CurrentSenseEnableDriver : public GpioOutputDriverBase
    {
    public:
        void enable(bool on) { set(on); }

    protected:
        GPIO_TypeDef *gpioPort() const override { return CUR_SENSE_ON_GPIO_Port; }
        uint16_t gpioPin() const override { return CUR_SENSE_ON_Pin; }
    };

} // namespace app

#endif // APP_DRIVERS_CURRENT_SENSE_ENABLE_DRIVER_HPP
