#ifndef APP_DRIVERS_SDADC_SE_INPUT_DRIVER_HPP
#define APP_DRIVERS_SDADC_SE_INPUT_DRIVER_HPP

#include <cstdint>
#include <array>
#include "driver.hpp"

extern "C" {
#include "main.h"
}

namespace app {

    class SdadcSeInputDriver : public IDriver
    {

    public:
        virtual ~SdadcSeInputDriver() = default;

    public:
        void init() override;
        void run() override;
        void setEnabled(bool value) override; // reset averaging window when toggled

    public:
        // Current processed value. If averaging mode is enabled, this is the running average; otherwise the last sample.
        int32_t value() const { return last_value_; }
        // Absolute volts (on demand). Returns NaN if no valid sample yet.
        float valueV() const;
        bool valid() const { return has_value_; }

        // Averaging controls
        void setAveragingEnabled(bool enabled);
        bool averagingEnabled() const { return averaging_enabled_; }
        void setAveragingWindow(uint16_t n); // number of samples (>=1), defaults to 20
        uint16_t averagingWindow() const { return avg_window_; }

    protected:
        virtual SDADC_HandleTypeDef *sdadc() const = 0;
        virtual uint32_t channel() const = 0;
        virtual int32_t calibrationOffset() const = 0;
        virtual float calibrationGain() const = 0;

    private:
        // moving-average bookkeeping
        static constexpr uint16_t kMaxAveragingWindow = 128;
        void resetAverager();
        std::array<int32_t, kMaxAveragingWindow> avg_buffer_{};
        uint16_t avg_head_{0};
        uint16_t avg_count_{0};
        int64_t avg_sum_{0};

        bool averaging_enabled_{false};
        uint16_t avg_window_{20};

        int32_t last_value_{0};
        bool has_value_{false};
    };

} // namespace app

#endif // APP_DRIVERS_SDADC_SE_INPUT_DRIVER_HPP
