#pragma once

namespace slstm32 {

class Driver {
public:
    virtual ~Driver() = default;
    virtual bool init() = 0;
    virtual void run() = 0;

    bool enabled() const { return enabled_; }
    virtual void setEnabled(bool enabled) { enabled_ = enabled; }

private:
    bool enabled_{true};
};

} // namespace slstm32
