#pragma once

#include <cstddef>
#include <cstdint>
#include "semantic_display/alert.hpp"
#include "slstm32/storage/byte_storage.hpp"
#include "slstm32/storage/redundant_blob_store.hpp"

namespace remote_a {

enum class BeepVolume : std::uint8_t { off, minimum, medium, maximum };
enum class LedMode : std::uint8_t { normal, subdued, off };

struct DeviceSettings {
    BeepVolume beepVolume{BeepVolume::minimum};
    LedMode ledMode{LedMode::normal};
    semantic_display::AlertLevel minimumAlertLevel{semantic_display::AlertLevel::info};
};

class DeviceSettingsStore {
public:
    DeviceSettingsStore(slstm32::storage::ByteStorage& storage,
                        std::uint32_t offset, std::size_t availableBytes)
        : store_(storage, offset, availableBytes) {}

    bool load(DeviceSettings& settings);
    bool save(const DeviceSettings& settings);

private:
    slstm32::storage::RedundantBlobStore store_;
};

} // namespace remote_a
