#pragma once

#include <cstddef>
#include <cstdint>
#include "device_shutdown_service.hpp"
#include "display_link_service.hpp"
#include "semantic_display/display.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"
#include "uart_transport.hpp"

namespace remote_a {

// The application's side of firmware updates over RS485 (semantic-link
// PROTOCOL.md, "Firmware update"): reports the running image, starts the
// bootloader on request (after showing that on the E-paper, which keeps the
// message during the update), and confirms a newly installed image once it
// has run normally for 10 s.
class FirmwareService final : public slstm32::Service {
public:
    FirmwareService(slstm32::Runtime runtime, semantic_display::DisplayService& display,
                    const semantic_display::PackageView& package, DeviceShutdownService& shutdown,
                    UartTransport& transport)
        : runtime_(runtime), display_(display), package_(package), shutdown_(shutdown), transport_(transport) {}

    // Before the display starts: the page shown before a firmware update
    // (same configuration only); the saved view is used once.
    static void restoreView(semantic_display::DisplayService& display, const semantic_display::PackageView& package);

    bool init() override { return runtime_.millis != nullptr; }
    void run() override;
    FirmwareMessageReceiver receiver() { return {this, &statusThunk, &enterBootloaderThunk}; }

private:
    static std::size_t statusThunk(void* context, std::uint8_t* output, std::size_t capacity);
    static void enterBootloaderThunk(void* context);

    slstm32::Runtime runtime_;
    semantic_display::DisplayService& display_;
    const semantic_display::PackageView& package_;
    DeviceShutdownService& shutdown_;
    UartTransport& transport_;
    std::uint32_t bootloaderAt_{};
    bool bootloaderRequested_{};
    bool confirmed_{};
};

} // namespace remote_a
