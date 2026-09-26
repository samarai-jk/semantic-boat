#include "drivers/sdadc_se_input_driver.hpp"
#include "lib.hpp"
#include <cmath>

namespace app
{

    void SdadcSeInputDriver::init()
    {
        // nothing to do
    }

    void SdadcSeInputDriver::setEnabled(bool value)
    {
        IDriver::setEnabled(value);
        // restart the averaging window to avoid mixing old samples after toggling
        resetAverager();
        has_value_ = false;
        last_value_ = 0;
    }

    void SdadcSeInputDriver::resetAverager()
    {
        avg_head_ = 0;
        avg_count_ = 0;
        avg_sum_ = 0;
        // keep buffer contents don't matter after count reset
    }

    void SdadcSeInputDriver::setAveragingEnabled(bool enabled)
    {
        if (averaging_enabled_ != enabled)
        {
            averaging_enabled_ = enabled;
            resetAverager();
        }
    }

    void SdadcSeInputDriver::setAveragingWindow(uint16_t n)
    {
        if (n == 0)
        {
            n = 1;
        }
        if (n > kMaxAveragingWindow)
        {
            n = kMaxAveragingWindow;
        }
        if (avg_window_ != n)
        {
            avg_window_ = n;
            resetAverager();
        }
    }

    void SdadcSeInputDriver::run()
    {
        if (!enabled())
        {
            return;
        }
        if (HAL_SDADC_AssociateChannelConfig(sdadc(), channel(), SDADC_CONF_INDEX_0) != HAL_OK)
        {
            swo_trace_line_level(LOG_ERROR, "HAL_SDADC_AssociateChannelConfig failed");
        }        
        if (HAL_SDADC_ConfigChannel(sdadc(), channel(), SDADC_CONTINUOUS_CONV_OFF) != HAL_OK)
        {
            swo_trace_line_level(LOG_ERROR, "HAL_SDADC_ConfigChannel failed");
            return;
        }        
        if (HAL_SDADC_Start(sdadc()) != HAL_OK)
        {
            swo_trace_line_level(LOG_ERROR, "HAL_SDADC_Start failed");
            return;
        }
        if (HAL_SDADC_PollForConversion(sdadc(), 10) == HAL_OK)
        {
            
            // sungle-ended zero reference
            uint32_t raw = HAL_SDADC_GetValue(sdadc());
            int16_t signed_raw = static_cast<int16_t>(raw & 0xFFFF); // sign-extend 16-bit result
            int32_t processed = static_cast<int32_t>((signed_raw - calibrationOffset()));
            processed = static_cast<int32_t>(((static_cast<int64_t>(processed) + 32768) * calibrationGain()) - 32768);
            if (processed < -32768) processed = -32768;
            if (processed > 32767) processed = 32767;

            if (averaging_enabled_)
            {
                // update moving average ring buffer
                if (avg_count_ < avg_window_)
                {
                    avg_buffer_[avg_head_] = processed;
                    avg_sum_ += processed;
                    avg_head_ = (avg_head_ + 1) % kMaxAveragingWindow;
                    avg_count_++;
                }
                else
                {
                    // overwrite oldest element
                    uint16_t tail = (avg_head_ + kMaxAveragingWindow - avg_window_) % kMaxAveragingWindow;
                    avg_sum_ -= avg_buffer_[tail];
                    avg_buffer_[avg_head_] = processed;
                    avg_sum_ += processed;
                    avg_head_ = (avg_head_ + 1) % kMaxAveragingWindow;
                }

                // compute average using current count (avoid division by zero)
                int32_t avg = static_cast<int32_t>(avg_sum_ / static_cast<int64_t>(avg_count_ == 0 ? 1 : avg_count_));
                last_value_ = avg;
            }
            else
            {
                last_value_ = processed;
            }

            has_value_ = true;            
            
            // sungle-ended offset
            // uint32_t raw = HAL_SDADC_GetValue(sdadc());
            // last_value_ = static_cast<int32_t>(raw);
            // has_value_ = true;
            
        }
    }

    float SdadcSeInputDriver::valueV() const
    {
        
        if (!has_value_)
        {
            return std::nanf("");
        }
        
        constexpr float v_ref = 1.8f;      
        constexpr float sdadc_gain = 1.0f;      
        
        // sungle-ended zero reference
        return static_cast<float>(last_value_ + 32768) * v_ref / (sdadc_gain * 65536);
        
        // sungle-ended offset
        //return static_cast<float>(last_value_) * v_ref / (2 * gain * 32768);
        
    }

} // namespace app
