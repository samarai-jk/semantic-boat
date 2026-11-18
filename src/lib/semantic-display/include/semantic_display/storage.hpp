#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include "semantic_display/package.hpp"
#include "slstm32/storage/byte_storage.hpp"

namespace semantic_display {

// The two-slot store of earlier firmware (one app, A/B copies). Read only to
// take its app over into the ConfigCache; the settings area at the end is
// shared by both layouts (and read by the remote-a bootloader).
struct ConfigStorageLayout {
    std::size_t storageBytes{32u * 1024u};
    std::size_t settingsBytes{256u};
    std::size_t slotHeaderBytes{64u};
    std::uint8_t slotCount{2u};

    constexpr std::size_t slotBytes() const { return (storageBytes - settingsBytes) / slotCount; }
    constexpr std::size_t maxPackageBytes() const { return slotBytes() - slotHeaderBytes; }
    constexpr std::size_t slotOffset(std::uint8_t slot) const { return static_cast<std::size_t>(slot) * slotBytes(); }
    constexpr std::size_t settingsOffset() const { return storageBytes - settingsBytes; }
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

inline constexpr std::size_t maxConfigNameBytes = 47u;
inline constexpr std::uint8_t maxConfigCacheSlots = 8u;

// Several compiled apps in fixed slots, each a header (name, checksums,
// generation) and the package. The newest generation is the active app.
struct ConfigCacheLayout {
    std::size_t storageBytes{32u * 1024u};
    std::size_t settingsBytes{256u};
    std::uint8_t slotCount{5u};
    std::size_t headerBytes{128u};

    constexpr std::size_t slotBytes() const { return (storageBytes - settingsBytes) / slotCount; }
    constexpr std::size_t maxPackageBytes() const { return slotBytes() - headerBytes; }
    constexpr std::size_t slotOffset(std::uint8_t slot) const { return static_cast<std::size_t>(slot) * slotBytes(); }
    constexpr std::size_t settingsOffset() const { return storageBytes - settingsBytes; }
};

struct CachedConfig {
    std::uint32_t generation{};
    std::uint32_t packageSize{};
    std::uint32_t packageCrc32{};
    std::uint32_t sourceCrc32{}; // CRC-32 of the JSON it was compiled from
    std::uint8_t slot{};
    std::uint8_t nameLength{};
    bool valid{};
    char name[maxConfigNameBytes + 1u]{};
    std::string_view nameView() const { return {name, nameLength}; }
};

// Power-safe: a new app is written into a free or the least recently used
// slot, never over the active one, and its commit marker comes last;
// activating rewrites one header (a torn write leaves that slot invalid and
// the previous app active).
class ConfigCache {
public:
    ConfigCache(slstm32::storage::ByteStorage& storage, ConfigCacheLayout layout = {},
                ConfigStorageLayout legacy = {})
        : storage_(storage), layout_(layout), legacy_(legacy) {}

    ConfigCacheLayout layout() const { return layout_; }
    // The valid apps, newest (active) first; false on a read error.
    bool list(CachedConfig* entries, std::size_t capacity, std::size_t& count);
    // The newest app whose package checks out. With no cached app yet, the
    // app of the two-slot store is taken over into a slot that does not
    // overlap its active copy (so earlier firmware can still read it).
    bool loadActive(std::uint8_t* output, std::size_t capacity, CachedConfig& loaded);
    // The newest entry of that name.
    bool find(std::string_view name, CachedConfig& found);
    // Stores a package as the newest app; older entries of the name go.
    bool store(std::string_view name, const std::uint8_t* package, std::size_t size, CachedConfig& stored);
    // Makes a cached app the newest after checking its package (read into
    // scratch).
    bool activate(const CachedConfig& entry, std::uint8_t* scratch, std::size_t capacity);

private:
    bool readHeader(std::uint8_t slot, CachedConfig& entry);
    bool readPackage(const CachedConfig& entry, std::uint8_t* output, std::size_t capacity);
    bool writeHeader(const CachedConfig& entry, bool committed);
    bool write(std::uint8_t slot, std::string_view name, const std::uint8_t* package, std::size_t size,
               std::uint32_t generation, CachedConfig& stored);
    bool erase(std::uint8_t slot);
    bool migrateLegacy(std::uint8_t* output, std::size_t capacity, CachedConfig& loaded);
    slstm32::storage::ByteStorage& storage_;
    ConfigCacheLayout layout_;
    ConfigStorageLayout legacy_;
};

} // namespace semantic_display
