#include "drivers/sdadc_se_input_driver.hpp"

namespace app {

void SdadcSeInputDriver::init() {
    // Associate channel to configuration index (prepared in MX_SDADC1_Init)
    (void)HAL_SDADC_AssociateChannelConfig(sdadc(), channel(), confIndex());
    // Select channel for regular conversion, single-shot (non-continuous)
    (void)HAL_SDADC_ConfigChannel(sdadc(), channel(), SDADC_CONTINUOUS_CONV_OFF);
}

void SdadcSeInputDriver::run() {
    // Start, poll, read, stop (blocking, lightweight)
    if (HAL_SDADC_Start(sdadc()) != HAL_OK) {
        return;
    }
    if (HAL_SDADC_PollForConversion(sdadc(), 10) == HAL_OK) {
        uint32_t raw = HAL_SDADC_GetValue(sdadc());
        last_value_ = static_cast<int32_t>(raw);
        has_value_ = true;
    }
    (void)HAL_SDADC_Stop(sdadc());
}

} // namespace app
