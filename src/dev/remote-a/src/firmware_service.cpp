#include "firmware_service.hpp"
#include "b1s/firmware_image.hpp"
#include "remote_a/bootloader.hpp"
#include "slstm32/build_profile.hpp"
extern "C" {
#include "main.h"
}

namespace remote_a {
namespace {

namespace image = b1s::image;

constexpr std::uint8_t versionMajor = 0u, versionMinor = 2u, versionPatch = 0u;
constexpr std::uint32_t confirmAfterMs = 10000u;
// Long enough for the E-paper to show the update screen before the reset.
constexpr std::uint32_t bootloaderDelayMs = 2500u;

std::uint32_t read32(std::uint32_t address) { return *reinterpret_cast<const volatile std::uint32_t*>(address); }

void writeBackup(volatile std::uint32_t& reg, std::uint32_t value) {
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    reg = value;
}

} // namespace

// Firmware image header for the bootloader; size and CRC are stamped in after
// the link (src/lib/b1s/FIRMWARE.md). remote-a has no B1S product code.
[[gnu::section(".image_header"), gnu::used]] const image::Header imageHeader = image::makeHeader(
    b1s::Board::remoteA, 0u, versionMajor, versionMinor, versionPatch, slstm32::devTools,
    slstm32::devTools ? "0.2.0-dev " __DATE__ : "0.2.0 " __DATE__);

std::size_t FirmwareService::statusThunk(void*, std::uint8_t* output, std::size_t capacity) {
    const auto header = image::applicationBase + image::headerOffset;
    const bool unconfirmed = (RTC->BKP8R >> 16u) == image::trialMagic;
    return semantic_link::encodeFirmwareStatus(
        output, capacity, semantic_link::FirmwareResult::done, semantic_link::FirmwareMode::application,
        static_cast<std::uint16_t>(b1s::Board::remoteA), 0u, read32(header + image::crcOffset), imageHeader.version,
        unconfirmed ? semantic_link::firmwareUnconfirmed : 0u, imageHeader.versionText);
}

void FirmwareService::enterBootloaderThunk(void* context) {
    auto& self = *static_cast<FirmwareService*>(context);
    if (self.bootloaderRequested_) return;
    self.bootloaderRequested_ = true;
    self.bootloaderAt_ = self.runtime_.millis() + bootloaderDelayMs;
    self.transport_.setTransferIndication(true);
    self.display_.showTransientModal("FIRMWARE UPDATE", "THE NEW FIRMWARE IS BEING INSTALLED. "
                                     "THE DISPLAY RESTARTS WHEN IT IS DONE.", "PLEASE WAIT");
}

void FirmwareService::run() {
    const auto now = runtime_.millis();
    if (!confirmed_ && now >= confirmAfterMs) {
        confirmed_ = true;
        if ((RTC->BKP8R >> 16u) == image::trialMagic) writeBackup(RTC->BKP8R, 0u);
    }
    if (!bootloaderRequested_ || static_cast<std::int32_t>(now - bootloaderAt_) < 0) return;
    bootloaderRequested_ = false;
    writeBackup(RTC->BKP10R, bootloader::addressMagic | transport_.address());
    writeBackup(RTC->BKP7R, image::bootRequestMagic);
    shutdown_.requestReset();
}

} // namespace remote_a
