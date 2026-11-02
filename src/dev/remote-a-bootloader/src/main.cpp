// remote-a bootloader (README.md): starts the application, or stays and
// receives a new one over RS485.
#include "b1s/firmware_image.hpp"
#include "device_settings.hpp"
#include "hardware.hpp"
#include "link.hpp"
#include "remote_a/bootloader.hpp"
#include "semantic_display/storage.hpp"
#include "version.hpp"

namespace {

namespace image = b1s::image;

constexpr unsigned requestRegister = 7u;
constexpr unsigned trialRegister = 8u;
constexpr unsigned trialCrcRegister = 9u;

std::uint32_t read32(std::uint32_t address) { return *reinterpret_cast<const volatile std::uint32_t*>(address); }

// The application's image is complete and unchanged; returns its CRC.
bool validApplication(std::uint32_t& crc) {
    const auto header = image::applicationBase + image::headerOffset;
    const auto size = read32(header + 12u);
    if (read32(header) != image::magic || (read32(header + 4u) & 0xFFFFu) != image::headerVersion ||
        (read32(header + 4u) >> 16u) != static_cast<std::uint32_t>(b1s::Board::remoteA) || size % 8u != 0u ||
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

// The display's settings EEPROM, read only.
class Eeprom final : public slstm32::storage::ByteStorage {
public:
    std::size_t capacity() const override { return 32u * 1024u; }
    bool read(std::uint32_t address, std::uint8_t* data, std::size_t size) override {
        while (size != 0u) {
            const auto amount = size < 255u ? size : 255u;
            if (!boot::hw::eepromRead(static_cast<std::uint16_t>(address), data, amount)) return false;
            address += static_cast<std::uint32_t>(amount);
            data += amount;
            size -= amount;
        }
        return true;
    }
    bool write(std::uint32_t, const std::uint8_t*, std::size_t) override { return false; }
};

// The RS485 address: handed over by the application, else from the display
// settings in the EEPROM (as the application reads them), else 1.
std::uint8_t rs485Address(std::uint32_t clock) {
    const auto handover = boot::hw::backupRegister(remote_a::bootloader::addressRegister);
    if ((handover & 0xFFFF0000u) == remote_a::bootloader::addressMagic && (handover & 0xFFu) != 0u &&
        (handover & 0xFFu) != 255u) {
        return static_cast<std::uint8_t>(handover);
    }
    boot::hw::startEeprom(clock);
    Eeprom eeprom;
    constexpr semantic_display::ConfigStorageLayout layout{};
    remote_a::DeviceSettingsStore store{eeprom, static_cast<std::uint32_t>(layout.settingsOffset()),
                                        layout.settingsBytes};
    remote_a::DeviceSettings settings{};
    if (store.load(settings)) return settings.rs485Address;
    boot::hw::log("bootloader: no settings in the EEPROM, RS485 address 1");
    return remote_a::DeviceSettings::minimumRs485Address;
}

} // namespace

// Never called: virtual destructors of ByteStorage would otherwise pull in the heap.
void operator delete(void*) noexcept {}
void operator delete(void*, std::size_t) noexcept {}

void bootMain() {
    const auto clock = boot::hw::startClock();
    boot::hw::startTick(clock);
    boot::hw::startLog(clock);
    const char* reason = stayReason();
    if (reason == nullptr) {
        boot::hw::log("bootloader: starting application");
        // A handed-over address is only for this stay; the settings may change.
        if (boot::hw::backupRegister(remote_a::bootloader::addressRegister) != 0u) {
            boot::hw::setBackupRegister(remote_a::bootloader::addressRegister, 0u);
        }
        boot::hw::startApplication(image::applicationBase);
    }
    boot::hw::log("");
    boot::hw::log("remote-a bootloader");
    boot::hw::log(bootloaderVersion);
    boot::hw::log(reason);
    boot::hw::startLed();
    boot::hw::startRs485(clock);
    boot::Link link{rs485Address(clock), reason};
    link.start();
    while (true) {
        link.run();
        // Two flashes a second while waiting, fast during an update.
        const auto period = link.updating() ? 100u : 500u;
        boot::hw::setLed(boot::hw::millis() % period < period / 5u);
    }
}
