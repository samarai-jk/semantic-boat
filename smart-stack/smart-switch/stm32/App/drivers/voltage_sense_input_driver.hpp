#ifndef APP_DRIVERS_VOLTAGE_SENSE_INPUT_DRIVER_HPP
#define APP_DRIVERS_VOLTAGE_SENSE_INPUT_DRIVER_HPP

#include "drivers/sdadc_se_input_driver.hpp"

extern "C"
{
#include "main.h"
    extern SDADC_HandleTypeDef hsdadc1;
}

namespace app
{

    // SDADC input driver for device voltage sense on PB1 (SDADC1 CH5P) -> MCU_ANA_DEV_V
    class VoltageSenseInputDriver : public SdadcSeInputDriver
    {
    protected:
        SDADC_HandleTypeDef *sdadc() const override { return &hsdadc1; }
        uint32_t channel() const override { return SDADC_CHANNEL_5; }
        virtual int32_t calibrationOffset() const override { return 0; };
        virtual float calibrationGain() const override { return 1.0f; };
    };

} // namespace app

#endif // APP_DRIVERS_VOLTAGE_SENSE_INPUT_DRIVER_HPP
