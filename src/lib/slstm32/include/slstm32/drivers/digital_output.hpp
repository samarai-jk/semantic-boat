#pragma once

#include "slstm32/driver.hpp"
#include "slstm32/drivers/hardware.hpp"

namespace slstm32::drivers {

class DigitalOutputDriver : public Driver {
public:
    explicit DigitalOutputDriver(DigitalOutputHardware hardware, bool initialValue = false)
        : hardware_(hardware), value_(initialValue) {}
    bool init() override;
    void run() override {}
    void set(bool value);
    void toggle() { set(!value_); }
    bool value() const { return value_; }

protected:
    void write(bool value);

private:
    DigitalOutputHardware hardware_;
    bool value_{};
};

} // namespace slstm32::drivers
