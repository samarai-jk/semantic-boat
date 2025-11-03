#ifndef APP_DRIVERS_GPIO_OUTPUT_BASE_HPP
#define APP_DRIVERS_GPIO_OUTPUT_BASE_HPP

#include <cstdint>
#include "driver.hpp"

extern "C"
{
#include "main.h"
}

namespace app
{

    class GpioOutputDriverBase : public IDriver
    {
    public:
        virtual ~GpioOutputDriverBase() = default;

        void init() override;
        void run() override {}

        void set(bool high);
        bool get() const { return state_; }

    protected:
        virtual GPIO_TypeDef *gpioPort() const = 0;
        virtual uint16_t gpioPin() const = 0;

    private:
        bool state_{false};
    };

} // namespace app

#endif // APP_DRIVERS_GPIO_OUTPUT_BASE_HPP
