#include "semantic_display/storage.hpp"
#include <cstring>

namespace semantic_display {
namespace {

constexpr std::uint32_t slotMagic = 0x31534253u; // "SBS1"
constexpr std::uint32_t commitMagic = 0x54494d43u; // "CMIT"
constexpr std::size_t generationOffset = 8u;
constexpr std::size_t packageSizeOffset = 12u;
constexpr std::size_t packageCrcOffset = 16u;
constexpr std::size_t sourceCrcOffset = 20u;
constexpr std::size_t commitOffset = 60u;

std::uint32_t read32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8u) |
           (static_cast<std::uint32_t>(data[2]) << 16u) | (static_cast<std::uint32_t>(data[3]) << 24u);
}
void write16(std::uint8_t* data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value); data[1] = static_cast<std::uint8_t>(value >> 8u);
}
void write32(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value); data[1] = static_cast<std::uint8_t>(value >> 8u);
    data[2] = static_cast<std::uint8_t>(value >> 16u); data[3] = static_cast<std::uint8_t>(value >> 24u);
}

} // namespace

bool ConfigStore::inspectSlot(std::uint8_t slot, StoredConfigInfo& info) {
    info = {};
    if (slot >= layout_.slotCount || layout_.slotHeaderBytes < 64u ||
        storage_.capacity() < layout_.storageBytes) return false;
    std::uint8_t header[64]{};
    const auto offset = layout_.slotOffset(slot);
    if (!storage_.read(static_cast<std::uint32_t>(offset), header, sizeof header)) return false;
    if (read32(header) != slotMagic || read32(header + commitOffset) != commitMagic ||
        read32(header + 4u) != packageFormatVersion) return true;
    const auto packageSize = read32(header + packageSizeOffset);
    if (packageSize < packageHeaderSize || packageSize > layout_.maxPackageBytes()) return true;
    info.generation = read32(header + generationOffset);
    info.packageSize = packageSize;
    info.sourceCrc32 = read32(header + sourceCrcOffset);
    info.slot = slot;
    info.valid = true;
    return true;
}

bool ConfigStore::inspect(StoredConfigInfo& newest) {
    newest = {};
    for (std::uint8_t slot = 0u; slot < layout_.slotCount; ++slot) {
        StoredConfigInfo candidate{};
        if (!inspectSlot(slot, candidate)) return false;
        if (candidate.valid && (!newest.valid ||
            static_cast<std::int32_t>(candidate.generation - newest.generation) > 0)) newest = candidate;
    }
    return true;
}

bool ConfigStore::load(std::uint8_t* output, std::size_t capacity, StoredConfigInfo& loaded) {
    if (!output) return false;
    StoredConfigInfo candidates[2]{};
    if (layout_.slotCount != 2u || !inspectSlot(0u, candidates[0]) ||
        !inspectSlot(1u, candidates[1])) return false;
    if (candidates[1].valid && (!candidates[0].valid ||
        static_cast<std::int32_t>(candidates[1].generation - candidates[0].generation) > 0)) {
        const auto temporary = candidates[0]; candidates[0] = candidates[1]; candidates[1] = temporary;
    }
    for (const auto& candidate : candidates) {
        if (!candidate.valid || candidate.packageSize > capacity) continue;
        const auto address = layout_.slotOffset(candidate.slot) + layout_.slotHeaderBytes;
        if (!storage_.read(static_cast<std::uint32_t>(address), output, candidate.packageSize)) continue;
        const PackageView package{output, candidate.packageSize};
        std::uint8_t header[64]{};
        if (!package.valid() ||
            !storage_.read(static_cast<std::uint32_t>(layout_.slotOffset(candidate.slot)),
                           header, sizeof header) ||
            crc32(output, candidate.packageSize) != read32(header + packageCrcOffset) ||
            package.info().sourceCrc32 != candidate.sourceCrc32) continue;
        loaded = candidate;
        return true;
    }
    loaded = {};
    return false;
}

bool ConfigStore::commit(const std::uint8_t* packageData, std::size_t size,
                         StoredConfigInfo& committed) {
    const PackageView package{packageData, size};
    if (!package.valid() || size > layout_.maxPackageBytes()) return false;
    StoredConfigInfo newest{};
    if (!inspect(newest)) return false;
    const auto target = static_cast<std::uint8_t>(newest.valid
        ? (newest.slot + 1u) % layout_.slotCount : 0u);
    const auto generation = newest.valid ? newest.generation + 1u : 1u;
    std::uint8_t header[64]{};
    write32(header, slotMagic);
    write32(header + 4u, packageFormatVersion);
    write32(header + generationOffset, generation);
    write32(header + packageSizeOffset, static_cast<std::uint32_t>(size));
    write32(header + packageCrcOffset, crc32(packageData, size));
    write32(header + sourceCrcOffset, package.info().sourceCrc32);
    write16(header + 24u, static_cast<std::uint16_t>(layout_.slotHeaderBytes));
    const auto slotAddress = layout_.slotOffset(target);
    if (!storage_.write(static_cast<std::uint32_t>(slotAddress), header, sizeof header) ||
        !storage_.write(static_cast<std::uint32_t>(slotAddress + layout_.slotHeaderBytes),
                        packageData, size)) return false;
    const auto marker = commitMagic;
    std::uint8_t commitBytes[4]{};
    write32(commitBytes, marker);
    if (!storage_.write(static_cast<std::uint32_t>(slotAddress + commitOffset),
                        commitBytes, sizeof commitBytes)) return false;
    committed = {generation, static_cast<std::uint32_t>(size), package.info().sourceCrc32,
                 target, true};
    return true;
}

namespace {

constexpr std::uint32_t cacheMagic = 0x31434253u; // "SBC1"
constexpr std::size_t cacheGenerationOffset = 8u;
constexpr std::size_t cacheSizeOffset = 12u;
constexpr std::size_t cachePackageCrcOffset = 16u;
constexpr std::size_t cacheSourceCrcOffset = 20u;
constexpr std::size_t cacheNameLengthOffset = 24u;
constexpr std::size_t cacheNameOffset = 25u;
constexpr std::size_t cacheHeaderCrcOffset = 120u; // CRC-32 of bytes 0..119
constexpr std::size_t cacheCommitOffset = 124u;
constexpr std::size_t cacheHeaderBytes = 128u;

// The cache runs on app switches only: small rather than fast.
bool newer(std::uint32_t a, std::uint32_t b) { return static_cast<std::int32_t>(a - b) > 0; }

} // namespace

__attribute__((optimize("Os"))) bool ConfigCache::readHeader(std::uint8_t slot, CachedConfig& entry) {
    entry = {};
    entry.slot = slot;
    if (slot >= layout_.slotCount || layout_.slotCount > maxConfigCacheSlots ||
        layout_.headerBytes != cacheHeaderBytes || storage_.capacity() < layout_.storageBytes) return false;
    std::uint8_t header[cacheHeaderBytes]{};
    if (!storage_.read(static_cast<std::uint32_t>(layout_.slotOffset(slot)), header, sizeof header)) return false;
    if (read32(header) != cacheMagic || read32(header + 4u) != packageFormatVersion ||
        read32(header + cacheCommitOffset) != commitMagic ||
        read32(header + cacheHeaderCrcOffset) != crc32(header, cacheHeaderCrcOffset)) return true;
    const auto size = read32(header + cacheSizeOffset);
    const auto nameLength = header[cacheNameLengthOffset];
    if (size < packageHeaderSize || size > layout_.maxPackageBytes() || nameLength > maxConfigNameBytes) return true;
    entry.generation = read32(header + cacheGenerationOffset);
    entry.packageSize = size;
    entry.packageCrc32 = read32(header + cachePackageCrcOffset);
    entry.sourceCrc32 = read32(header + cacheSourceCrcOffset);
    entry.nameLength = nameLength;
    std::memcpy(entry.name, header + cacheNameOffset, nameLength);
    entry.valid = true;
    return true;
}

__attribute__((optimize("Os"))) bool ConfigCache::writeHeader(const CachedConfig& entry, bool committed) {
    std::uint8_t header[cacheHeaderBytes]{};
    write32(header, cacheMagic);
    write32(header + 4u, packageFormatVersion);
    write32(header + cacheGenerationOffset, entry.generation);
    write32(header + cacheSizeOffset, entry.packageSize);
    write32(header + cachePackageCrcOffset, entry.packageCrc32);
    write32(header + cacheSourceCrcOffset, entry.sourceCrc32);
    header[cacheNameLengthOffset] = entry.nameLength;
    std::memcpy(header + cacheNameOffset, entry.name, entry.nameLength);
    write32(header + cacheHeaderCrcOffset, crc32(header, cacheHeaderCrcOffset));
    if (committed) write32(header + cacheCommitOffset, commitMagic);
    return storage_.write(static_cast<std::uint32_t>(layout_.slotOffset(entry.slot)), header, sizeof header);
}

__attribute__((optimize("Os"))) bool ConfigCache::erase(std::uint8_t slot) {
    const std::uint8_t zero[4]{};
    return storage_.write(static_cast<std::uint32_t>(layout_.slotOffset(slot)), zero, sizeof zero);
}

__attribute__((optimize("Os"))) bool ConfigCache::readPackage(const CachedConfig& entry, std::uint8_t* output, std::size_t capacity) {
    if (!output || !entry.valid || entry.packageSize > capacity ||
        !storage_.read(static_cast<std::uint32_t>(layout_.slotOffset(entry.slot) + layout_.headerBytes),
                       output, entry.packageSize)) return false;
    const PackageView package{output, entry.packageSize};
    return crc32(output, entry.packageSize) == entry.packageCrc32 && package.valid() &&
           package.info().sourceCrc32 == entry.sourceCrc32;
}

__attribute__((optimize("Os"))) bool ConfigCache::list(CachedConfig* entries, std::size_t capacity, std::size_t& count) {
    count = 0u;
    for (std::uint8_t slot = 0u; slot < layout_.slotCount; ++slot) {
        CachedConfig entry{};
        if (!readHeader(slot, entry)) return false;
        if (!entry.valid || count == capacity) continue;
        // Insert sorted, newest first.
        auto position = count;
        while (position != 0u && newer(entry.generation, entries[position - 1u].generation)) {
            entries[position] = entries[position - 1u];
            --position;
        }
        entries[position] = entry;
        ++count;
    }
    return true;
}

__attribute__((optimize("Os"))) bool ConfigCache::find(std::string_view name, CachedConfig& found) {
    CachedConfig entries[maxConfigCacheSlots]{};
    std::size_t count{};
    if (!list(entries, maxConfigCacheSlots, count)) return false;
    for (std::size_t index = 0u; index < count; ++index) {
        if (entries[index].nameView() == name) { found = entries[index]; return true; }
    }
    return false;
}

__attribute__((optimize("Os"))) bool ConfigCache::write(std::uint8_t slot, std::string_view name, const std::uint8_t* package, std::size_t size,
                        std::uint32_t generation, CachedConfig& stored) {
    const PackageView view{package, size};
    if (!view.valid() || size > layout_.maxPackageBytes() || name.size() > maxConfigNameBytes) return false;
    CachedConfig entry{};
    entry.generation = generation;
    entry.packageSize = static_cast<std::uint32_t>(size);
    entry.packageCrc32 = crc32(package, size);
    entry.sourceCrc32 = view.info().sourceCrc32;
    entry.slot = slot;
    entry.nameLength = static_cast<std::uint8_t>(name.size());
    std::memcpy(entry.name, name.data(), name.size());
    // Header without its commit marker first (the slot is invalid from
    // here on), then the package, then the marker.
    if (!writeHeader(entry, false) ||
        !storage_.write(static_cast<std::uint32_t>(layout_.slotOffset(slot) + layout_.headerBytes), package, size)) {
        return false;
    }
    std::uint8_t marker[4]{};
    write32(marker, commitMagic);
    if (!storage_.write(static_cast<std::uint32_t>(layout_.slotOffset(slot) + cacheCommitOffset), marker,
                        sizeof marker)) return false;
    entry.valid = true;
    stored = entry;
    return true;
}

__attribute__((optimize("Os"))) bool ConfigCache::store(std::string_view name, const std::uint8_t* package, std::size_t size,
                        CachedConfig& stored) {
    CachedConfig entries[maxConfigCacheSlots]{};
    std::size_t count{};
    if (!list(entries, maxConfigCacheSlots, count)) return false;
    bool used[maxConfigCacheSlots]{};
    for (std::size_t index = 0u; index < count; ++index) used[entries[index].slot] = true;
    // A free slot, else an older entry of this name, else the least
    // recently used one; never the active (newest) app.
    std::uint8_t target = maxConfigCacheSlots;
    for (std::uint8_t slot = 0u; slot < layout_.slotCount && target == maxConfigCacheSlots; ++slot) {
        if (!used[slot]) target = slot;
    }
    for (std::size_t index = 1u; index < count && target == maxConfigCacheSlots; ++index) {
        if (entries[index].nameView() == name) target = entries[index].slot;
    }
    if (target == maxConfigCacheSlots && count > 1u) target = entries[count - 1u].slot;
    if (target == maxConfigCacheSlots) return false;
    const auto generation = count != 0u ? entries[0].generation + 1u : 1u;
    if (!write(target, name, package, size, generation, stored)) return false;
    // Older entries of the name (the previous version) are no longer needed.
    for (std::size_t index = 0u; index < count; ++index) {
        if (entries[index].slot != target && entries[index].nameView() == name) (void)erase(entries[index].slot);
    }
    return true;
}

__attribute__((optimize("Os"))) bool ConfigCache::activate(const CachedConfig& entry, std::uint8_t* scratch, std::size_t capacity) {
    CachedConfig current{};
    if (!readHeader(entry.slot, current) || !current.valid || current.generation != entry.generation ||
        !readPackage(current, scratch, capacity)) return false;
    CachedConfig entries[maxConfigCacheSlots]{};
    std::size_t count{};
    if (!list(entries, maxConfigCacheSlots, count) || count == 0u) return false;
    if (entries[0].slot == current.slot) return true;
    current.generation = entries[0].generation + 1u;
    return writeHeader(current, true);
}

__attribute__((optimize("Os"))) bool ConfigCache::loadActive(std::uint8_t* output, std::size_t capacity, CachedConfig& loaded) {
    loaded = {};
    CachedConfig entries[maxConfigCacheSlots]{};
    std::size_t count{};
    if (!list(entries, maxConfigCacheSlots, count)) return false;
    for (std::size_t index = 0u; index < count; ++index) {
        if (readPackage(entries[index], output, capacity)) { loaded = entries[index]; return true; }
    }
    return count == 0u && migrateLegacy(output, capacity, loaded);
}

__attribute__((optimize("Os"))) bool ConfigCache::migrateLegacy(std::uint8_t* output, std::size_t capacity, CachedConfig& loaded) {
    ConfigStore legacy{storage_, legacy_};
    StoredConfigInfo info{};
    if (!legacy.load(output, capacity, info)) return false;
    // The first slot clear of the legacy copy just loaded.
    const auto legacyStart = legacy_.slotOffset(info.slot);
    const auto legacyEnd = legacyStart + legacy_.slotBytes();
    for (std::uint8_t slot = 0u; slot < layout_.slotCount; ++slot) {
        const auto start = layout_.slotOffset(slot);
        if (start < legacyEnd && legacyStart < start + layout_.slotBytes()) continue;
        if (info.packageSize <= layout_.maxPackageBytes() && write(slot, {}, output, info.packageSize, 1u, loaded)) {
            return true;
        }
        break;
    }
    // Run it anyway; it is taken over at the next start.
    loaded = {};
    loaded.packageSize = info.packageSize;
    loaded.sourceCrc32 = info.sourceCrc32;
    return true;
}

} // namespace semantic_display
