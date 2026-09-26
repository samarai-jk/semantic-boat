#ifndef APP_DRIVERS_CURRENT_SENSE_INPUT_DRIVER_HPP
#define APP_DRIVERS_CURRENT_SENSE_INPUT_DRIVER_HPP

#include "drivers/sdadc_se_input_driver.hpp"

extern "C"
{
#include "main.h"
    extern SDADC_HandleTypeDef hsdadc1;
}

namespace app
{

    // SDADC input driver for device current sense on PB2 (SDADC1 CH4P) -> MCU_ANA_DEV_I
    class CurrentSenseInputDriver : public SdadcSeInputDriver
    {
    protected:
        SDADC_HandleTypeDef *sdadc() const override { return &hsdadc1; }
        uint32_t channel() const override { return SDADC_CHANNEL_4; }
        virtual int32_t calibrationOffset() const override { return 800; };
        virtual float calibrationGain() const override { return 0.97f; };
    };

} // namespace app

#endif // APP_DRIVERS_CURRENT_SENSE_INPUT_DRIVER_HPP
