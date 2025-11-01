#include "services/device_manager.hpp"
#include "log.hpp"

namespace app {

void DeviceManager::init() {
    // Heartbeat every 1s
    heartbeat_ = Executor::instance().callEvery(1000, &DeviceManager::heartbeatTask, this);
}

void DeviceManager::run() {
    // Could poll system health if needed
}

void DeviceManager::setError(const ErrorEvent& ev) {
    if (!in_error_) {
        in_error_ = true;
        ErrorReporter::instance().report(ev);
        log_write(LogLevel::Error, "Entering ERROR state: services will be limited");
        // TODO: disable non-essential services via a ServiceManager when present
    }
}

void DeviceManager::heartbeatTask(void* ctx) {
    auto* self = static_cast<DeviceManager*>(ctx);
    if (!self) return;
    if (!self->in_error_) {
        log_write(LogLevel::Info, "HB");
    }
}

} // namespace app
