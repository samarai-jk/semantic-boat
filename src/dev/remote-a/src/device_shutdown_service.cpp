#include "device_shutdown_service.hpp"
extern "C" {
#include "main.h"
}

namespace remote_a {

void DeviceShutdownService::requestReset() {
    if (active_) return;
    active_ = true;

    // The unsubscribe frame is transmitted synchronously. Only after it has
    // left the UART do we disable the transport and power down local outputs.
    link_.shutdown();
    transport_.setSleeping(true);
    feedback_.shutdown();
    display_.requestShutdown();
}

void DeviceShutdownService::run() {
    if (!active_ || !display_.sleeping()) return;
    NVIC_SystemReset();
}

} // namespace remote_a
