#ifndef APP_SERVICES_DEVICE_MANAGER_HPP
#define APP_SERVICES_DEVICE_MANAGER_HPP

#include <cstdint>
#include "driver.hpp"
#include "executor.hpp"
#include "error.hpp"

namespace app {

class DeviceManager : public IDriver {
public:
    void init() override;
    void run() override;

    bool inError() const { return in_error_; }
    void setError(const ErrorEvent& ev);

private:
    bool in_error_{false};
    TimerHandle heartbeat_{-1};

    static void heartbeatTask(void* ctx);
};

} // namespace app

#endif // APP_SERVICES_DEVICE_MANAGER_HPP
