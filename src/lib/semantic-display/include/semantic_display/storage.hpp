#pragma once

#include <cstddef>
#include <cstdint>
#include "semantic_display/package.hpp"
#include "slstm32/storage/byte_storage.hpp"

namespace semantic_display {

struct ConfigStorageLayout {
    std::size_t storageBytes{32u * 1024u};
    std::size_t settingsBytes{256u};
    std::size_t slotHeaderBytes{64u};
    std::uint8_t slotCount{2u};

    constexpr std::size_t slotBytes() const { return (storageBytes - settingsBytes) / slotCount; }
    constexpr std::size_t maxPackageBytes() const { return slotBytes() - slotHeaderBytes; }
    constexpr std::size_t slotOffset(std::uint8_t slot) const { return static_cast<std::size_t>(slot) * slotBytes(); }
    constexpr std::size_t settingsOffset() const { return slotBytes() * slotCount; }
};

struct StoredConfigInfo {
    std::uint32_t generation{};
    std::uint32_t packageSize{};
    std::uint32_t sourceCrc32{};
    std::uint8_t slot{};
    bool valid{};
};

class ConfigStore {
public:
    ConfigStore(slstm32::storage::ByteStorage& storage, ConfigStorageLayout layout = {})
        : storage_(storage), layout_(layout) {}

    ConfigStorageLayout layout() const { return layout_; }
    bool inspect(StoredConfigInfo& newest);
    bool load(std::uint8_t* output, std::size_t capacity, StoredConfigInfo& loaded);
    bool commit(const std::uint8_t* package, std::size_t size, StoredConfigInfo& committed);

private:
    bool inspectSlot(std::uint8_t slot, StoredConfigInfo& info);
    slstm32::storage::ByteStorage& storage_;
    ConfigStorageLayout layout_;
};

} // namespace semantic_display
