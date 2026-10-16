#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include "device_settings.hpp"

namespace {

class MemoryStorage final : public slstm32::storage::ByteStorage {
public:
    std::size_t capacity() const override { return bytes_.size(); }

    bool read(std::uint32_t address, std::uint8_t* data, std::size_t size) override {
        if (address > bytes_.size() || size > bytes_.size() - address) return false;
        std::memcpy(data, bytes_.data() + address, size);
        return true;
    }

    bool write(std::uint32_t address, const std::uint8_t* data,
               std::size_t size) override {
        if (address > bytes_.size() || size > bytes_.size() - address) return false;
        std::memcpy(bytes_.data() + address, data, size);
        return true;
    }

private:
    std::array<std::uint8_t, slstm32::storage::RedundantBlobStore::requiredBytes> bytes_{};
};

void legacySettingsMigrate() {
    for (std::uint8_t version = 1u; version <= 4u; ++version) {
        MemoryStorage memory;
        slstm32::storage::RedundantBlobStore raw(memory, 0u, memory.capacity());
        const std::uint8_t legacy[]{version, 2u, 1u, 2u, 1u, 1u};
        assert(raw.commit(legacy, version + 2u));
        remote_a::DeviceSettingsStore store(memory, 0u, memory.capacity());
        remote_a::DeviceSettings loaded;
        loaded.rs485Address = 99u;
        assert(store.load(loaded));
        assert(loaded.rs485Address == 1u);
        assert(loaded.beepVolume == remote_a::BeepVolume::medium);
        assert(loaded.ledMode == remote_a::LedMode::subdued);
        assert(loaded.minimumAlertLevel == (version >= 2u ?
            semantic_display::AlertLevel::alarm : semantic_display::AlertLevel::info));
        assert(loaded.developerMode == (version >= 3u));
        assert(loaded.sleeping == (version >= 4u));
        loaded.rs485Address = 2u;
        assert(store.save(loaded));
        remote_a::DeviceSettings reloaded;
        assert(store.load(reloaded));
        assert(reloaded.rs485Address == 2u);
        assert(reloaded.beepVolume == loaded.beepVolume);
        assert(reloaded.ledMode == loaded.ledMode);
        assert(reloaded.minimumAlertLevel == loaded.minimumAlertLevel);
        assert(reloaded.developerMode == loaded.developerMode);
        assert(reloaded.sleeping == loaded.sleeping);
    }
}

void addressRangeIsValidated() {
    for (unsigned address = 0u; address <= 255u; ++address) {
        MemoryStorage memory;
        remote_a::DeviceSettingsStore store(memory, 0u, memory.capacity());
        remote_a::DeviceSettings settings;
        settings.rs485Address = static_cast<std::uint8_t>(address);
        const bool valid = address >= 1u && address <= 254u;
        assert(store.save(settings) == valid);
        if (valid) {
            remote_a::DeviceSettings loaded;
            assert(store.load(loaded));
            assert(loaded.rs485Address == address);
        } else {
            slstm32::storage::RedundantBlobStore raw(memory, 0u, memory.capacity());
            const std::uint8_t payload[]{5u, 1u, 0u, 0u, 0u, 0u,
                static_cast<std::uint8_t>(address)};
            assert(raw.commit(payload, sizeof payload));
            assert(!store.load(settings));
        }
    }
}

} // namespace

int main() {
    legacySettingsMigrate();
    addressRangeIsValidated();
    std::cout << "PASS: settings migration and RS485 address persistence\n";
}