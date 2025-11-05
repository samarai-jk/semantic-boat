#ifndef APP_DRIVER_FACTORY_HPP
#define APP_DRIVER_FACTORY_HPP

#include <cstdint>
#include "driver.hpp"

namespace app
{
    
    enum : uint16_t
    {
        DRV_RGB_LED,
        DRV_UI_HARDWARE,
        DRV_DEVICE,
        DRV_TEMP_SENSE_EN,
        DRV_DEV_TEMP_SENSE_IN,
        DRV_LATCHING_RELAY,
        DRV_CUR_SENSE_EN,
        DRV_CUR_SENSE_IN,
        DRV_VOL_SENSE_IN,
    };

    IDriver *createDriver(uint16_t id);

} // namespace app

#endif // APP_DRIVER_FACTORY_HPP