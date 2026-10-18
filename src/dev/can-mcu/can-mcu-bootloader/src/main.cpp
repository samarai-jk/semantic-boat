// can-mcu bootloader (src/lib/b1s/FIRMWARE.md): starts the application, or
// stays and receives a new one over B1S.
#include "can_mcu/image_header.hpp"
#include "hardware.hpp"
#include "node.hpp"
#include "version.hpp"

namespace {

namespace image = can_mcu::image;

constexpr unsigned requestRegister = 7u;
constexpr unsigned trialRegister = 8u;
constexpr unsigned trialCrcRegister = 9u;

std::uint32_t read32(std::uint32_t address) { return *reinterpret_cast<const volatile std::uint32_t*>(address); }

// The application's image is complete and unchanged; returns its CRC.
bool validApplication(std::uint32_t& crc) {
    const auto header = image::applicationBase + image::headerOffset;
    const auto size = read32(header + 12u);
    if (read32(header) != image::magic || (read32(header + 4u) & 0xFFFFu) != image::headerVersion ||
        (read32(header + 4u) >> 16u) != image::boardCanMcuV1 || size % 8u != 0u ||
        size < image::headerOffset + sizeof(image::Header) || size > image::applicationSize) {
        return false;
    }
    // Stack pointer in RAM, reset vector inside the image.
    const auto stack = read32(image::applicationBase);
    const auto entry = read32(image::applicationBase + 4u) & ~1u;
    if (stack < 0x20000000u || stack > 0x2000C000u || entry < image::applicationBase ||
        entry >= image::applicationBase + size) {
        return false;
    }
    crc = image::imageCrc(reinterpret_cast<const std::uint8_t*>(image::applicationBase), size);
    return crc == read32(header + 16u);
}

// Why the bootloader stays, or nullptr to start the application.
const char* stayReason() {
    if (boot::hw::backupRegister(requestRegister) == image::bootRequestMagic) {
        boot::hw::setBackupRegister(requestRegister, 0u);
        return "update requested";
    }
    std::uint32_t crc = 0u;
    if (!validApplication(crc)) return "no valid application";
    const auto trial = boot::hw::backupRegister(trialRegister);
    if ((trial >> 16u) == image::trialMagic) {
        // A new image counts its starts until it confirms itself; another
        // image (flashed with the ST-Link) ends the trial.
        if (boot::hw::backupRegister(trialCrcRegister) != crc) {
            boot::hw::setBackupRegister(trialRegister, 0u);
        } else if ((trial & 0xFFu) >= image::maxTrialStarts) {
            return "application not confirmed";
        } else {
            boot::hw::setBackupRegister(trialRegister, trial + 1u);
        }
    }
    return nullptr;
}

} // namespace

void bootMain() {
    const auto clock = boot::hw::startClock();
    boot::hw::startTick(clock);
    boot::hw::startLog(clock);
    const char* reason = stayReason();
    if (reason == nullptr) {
        boot::hw::log("bootloader: starting application");
        boot::hw::startApplication(image::applicationBase);
    }
    boot::hw::log("");
    boot::hw::log("can-mcu bootloader");
    boot::hw::log(bootloaderVersion);
    boot::hw::log(reason);
    if (!boot::hw::startCan(clock)) boot::hw::log("bootloader: CAN did not start");
    boot::Node node{reason};
    node.start();
    while (true) node.run();
}
