#pragma once

// The application's side of firmware updates over CAN (src/lib/b1s/FIRMWARE.md).
namespace can_mcu {

// Restarts into the bootloader, which then waits for a new image.
[[noreturn]] void restartIntoBootloader();

// Confirms a newly installed image once the application runs normally;
// without it the bootloader takes over after three starts. Harmless when
// there is nothing to confirm.
void confirmFirmware();

} // namespace can_mcu
