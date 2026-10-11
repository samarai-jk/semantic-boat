#pragma once

#include "slstm32/driver.hpp"
#include "slstm32/drivers/hardware.hpp"

namespace slstm32::drivers {

class DigitalInputDriver : public Driver {
public:
    explicit DigitalInputDriver(DigitalInputHardware hardware) : hardware_(hardware) {}
    bool init() override;
    void run() override;
    bool value() const { return value_; }

private:
    DigitalInputHardware hardware_;
    bool value_{};
};

} // namespace slstm32::drivers
