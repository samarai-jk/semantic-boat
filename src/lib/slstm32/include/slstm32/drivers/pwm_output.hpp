#pragma once

#include "slstm32/driver.hpp"
#include "slstm32/drivers/hardware.hpp"

namespace slstm32::drivers {

class PwmOutputDriver final : public Driver {
public:
    explicit PwmOutputDriver(PwmOutputHardware hardware, float initialDuty = 0.0f)
        : hardware_(hardware), duty_(initialDuty) {}
    bool init() override;
    void run() override {}
    void setDuty(float duty);
    float duty() const { return duty_; }

private:
    PwmOutputHardware hardware_;
    float duty_{};
};

} // namespace slstm32::drivers
