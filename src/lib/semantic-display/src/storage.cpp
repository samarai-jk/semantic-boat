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

} // namespace semantic_display
