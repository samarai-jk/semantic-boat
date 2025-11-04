#ifndef APP_DRIVER_STORE_HPP
#define APP_DRIVER_STORE_HPP

#include "driver_factory.hpp"
#include "driver_manager.hpp"

#include "drivers/rgb_led_driver.hpp"
#include "drivers/ui_hardware_driver.hpp"
#include "drivers/device_driver.hpp"
#include "drivers/temp_sense_enable_driver.hpp"
#include "drivers/dev_temp_sense_input_driver.hpp"

namespace app
{

    // *********************************************************************************************
     
    RgbLedDriver& getRgbLedDriver()
    {
        return *static_cast<RgbLedDriver*>(
            DriverManager::instance().get(DRV_RGB_LED)
        );
    }

    UiHardwareDriver& getUiHardwareDriver()
    {
        return *static_cast<UiHardwareDriver*>(
            DriverManager::instance().get(DRV_UI_HARDWARE)
        );
    }

    DeviceDriver& getDeviceDriver()
    {
        return *static_cast<DeviceDriver*>(
            DriverManager::instance().get(DRV_DEVICE)
        );
    }

    TempSenseEnableDriver& getTempSenseEnableDriver()
    {
        return *static_cast<TempSenseEnableDriver*>(
            DriverManager::instance().get(DRV_TEMP_SENSE_EN)
        );
    }
    
    DevTempSenseInputDriver& getDevTempSenseInputDriver()
    {
        return *static_cast<DevTempSenseInputDriver*>(
            DriverManager::instance().get(DRV_DEV_TEMP_SENSE_IN)
        );
    }
    
    // *********************************************************************************************
    
} // namespace app

#endif // APP_DRIVER_MANAGER_HPP