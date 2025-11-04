#include "drivers/sdadc_se_input_driver.hpp"
#include "lib.hpp"
#include <cmath>

namespace app
{

    void SdadcSeInputDriver::init()
    {
        if (HAL_SDADC_AssociateChannelConfig(sdadc(), channel(), SDADC_CONF_INDEX_0) != HAL_OK)
        {
            swo_trace_line_level(LOG_ERROR, "HAL_SDADC_AssociateChannelConfig failed");
        }
        if (HAL_SDADC_ConfigChannel(sdadc(), channel(), SDADC_CONTINUOUS_CONV_OFF) != HAL_OK)
        {
            swo_trace_line_level(LOG_ERROR, "HAL_SDADC_ConfigChannel failed");
        }
    }

    void SdadcSeInputDriver::run()
    {
        if (!enabled())
        {
            return;
        }
        if (HAL_SDADC_Start(sdadc()) != HAL_OK)
        {
            swo_trace_line_level(LOG_ERROR, "HAL_SDADC_Start failed");
            return;
        }
        if (HAL_SDADC_PollForConversion(sdadc(), 10) == HAL_OK)
        {
            uint32_t raw = HAL_SDADC_GetValue(sdadc());
            last_value_ = static_cast<int32_t>(raw);
            has_value_ = true;
        }
        if (HAL_SDADC_Stop_IT(sdadc()) != HAL_OK) {
            //swo_trace_line_level(LOG_ERROR, "HAL_SDADC_Stop failed");
        }
    }

    float SdadcSeInputDriver::valueV() const
    {
        if (!has_value_)
        {
            return std::nanf("");
        }
        // SDADC configured for:
        // - ReferenceVoltage = VREFINT2 (1.8 V)
        // - InputMode = SE_OFFSET
        // - Gain = 1
        // In this configuration, raw data spans 0 .. 65535 corresponding to 0 .. Vref.
        constexpr float kVref = 3.3f;      
        constexpr float kDenom = 65535.0f / 2; 
        return (last_value_ / kDenom) * kVref * 2;
    }

} // namespace app
