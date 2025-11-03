#ifndef APP_DRIVERS_DEV_TEMP_SENSE_INPUT_DRIVER_HPP
#define APP_DRIVERS_DEV_TEMP_SENSE_INPUT_DRIVER_HPP

#include "drivers/sdadc_se_input_driver.hpp"

extern "C" {
#include "main.h"
extern SDADC_HandleTypeDef hsdadc1;
}

namespace app {

// SDADC input driver for device temperature sense on PE9 (SDADC1 CH7P)
class DevTempSenseInputDriver : public SdadcSeInputDriver {
protected:
    SDADC_HandleTypeDef* sdadc() const override { return &hsdadc1; }
    uint32_t channel() const override { return SDADC_CHANNEL_7; }
    uint32_t confIndex() const override { return SDADC_CONF_INDEX_0; }
};

} // namespace app

#endif // APP_DRIVERS_DEV_TEMP_SENSE_INPUT_DRIVER_HPP
