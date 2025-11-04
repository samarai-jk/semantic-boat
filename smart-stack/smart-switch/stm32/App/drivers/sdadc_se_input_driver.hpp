#ifndef APP_DRIVERS_SDADC_SE_INPUT_DRIVER_HPP
#define APP_DRIVERS_SDADC_SE_INPUT_DRIVER_HPP

#include <cstdint>
#include "driver.hpp"

extern "C" {
#include "main.h"
}

namespace app {

class SdadcSeInputDriver : public IDriver {
public:
    virtual ~SdadcSeInputDriver() = default;
    void init() override;
    void run() override;
    int32_t value() const { return last_value_; }
    // Absolute volts (on demand). Returns NaN if no valid sample yet.
    float valueV() const;
    bool valid() const { return has_value_; }
protected:
    virtual SDADC_HandleTypeDef* sdadc() const = 0;
    virtual uint32_t channel() const = 0;
private:
    int32_t last_value_{0};
    bool has_value_{false};
};

} // namespace app

#endif // APP_DRIVERS_SDADC_SE_INPUT_DRIVER_HPP
