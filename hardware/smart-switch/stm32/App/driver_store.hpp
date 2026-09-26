#ifndef APP_DRIVER_STORE_HPP
#define APP_DRIVER_STORE_HPP

#include "driver_factory.hpp"
#include "driver_manager.hpp"

#include "drivers/rgb_led_driver.hpp"
#include "drivers/ui_hardware_driver.hpp"
#include "drivers/device_driver.hpp"
#include "drivers/temp_sense_enable_driver.hpp"
#include "drivers/dev_temp_sense_input_driver.hpp"
#include "drivers/current_sense_enable_driver.hpp"
#include "drivers/current_sense_input_driver.hpp"
#include "drivers/latching_relay_driver.hpp"
#include "drivers/voltage_sense_input_driver.hpp"
#include "drivers/smbus_driver.hpp"

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

    CurrentSenseEnableDriver& getCurrentSenseEnableDriver()
    {
        return *static_cast<CurrentSenseEnableDriver*>(
            DriverManager::instance().get(DRV_CUR_SENSE_EN)
        );
    }
    
    CurrentSenseInputDriver& getCurrentSenseInputDriver()
    {
        return *static_cast<CurrentSenseInputDriver*>(
            DriverManager::instance().get(DRV_CUR_SENSE_IN)
        );
    }
    
    LatchingRelayDriver& getLatchingRelayDriver()
    {
        return *static_cast<LatchingRelayDriver*>(
            DriverManager::instance().get(DRV_LATCHING_RELAY)
        );
    }

    VoltageSenseInputDriver& getVoltageSenseInputDriver()
    {
        return *static_cast<VoltageSenseInputDriver*>(
            DriverManager::instance().get(DRV_VOL_SENSE_IN)
        );
    }
    
    SmBusDriver& getSmBusDriver()
    {
        return *static_cast<SmBusDriver*>(
            DriverManager::instance().get(DRV_SMBUS)
        );
    }
    
    // *********************************************************************************************
    
} // namespace app

#endif // APP_DRIVER_MANAGER_HPP