#include "slstm32/storage/redundant_blob_store.hpp"
#include <cstring>

namespace slstm32::storage {
namespace {

constexpr std::uint32_t recordMagic = 0x31534c53u; // "SLS1"
constexpr std::uint16_t recordVersion = 1u;
constexpr std::uint32_t commitMagic = 0x54494d43u; // "CMIT"
constexpr std::size_t payloadOffset = 16u;
constexpr std::size_t commitOffset = 60u;

std::uint16_t read16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8u);
}

std::uint32_t read32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8u) |
           (static_cast<std::uint32_t>(data[2]) << 16u) |
           (static_cast<std::uint32_t>(data[3]) << 24u);
}

void write16(std::uint8_t* data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8u);
}

void write32(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8u);
    data[2] = static_cast<std::uint8_t>(value >> 16u);
    data[3] = static_cast<std::uint8_t>(value >> 24u);
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t index = 0u; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

} // namespace

bool RedundantBlobStore::layoutValid() const {
    return availableBytes_ >= requiredBytes && offset_ <= storage_.capacity() &&
           requiredBytes <= storage_.capacity() - offset_;
}

bool RedundantBlobStore::inspect(std::uint8_t slot, std::uint8_t* record,
                                 SlotInfo& info) {
    info = {};
    if (!layoutValid() || slot >= 2u || !record ||
        !storage_.read(offset_ + static_cast<std::uint32_t>(slot * slotBytes),
                       record, slotBytes)) return false;
    if (read32(record) != recordMagic || read16(record + 4u) != recordVersion ||
        read32(record + commitOffset) != commitMagic) return true;
    const auto payloadSize = read16(record + 6u);
    if (payloadSize == 0u || payloadSize > maxPayloadBytes ||
        crc32(record + payloadOffset, payloadSize) != read32(record + 12u)) return true;
    info.generation = read32(record + 8u);
    info.payloadSize = payloadSize;
    info.slot = slot;
    info.valid = true;
    return true;
}

bool RedundantBlobStore::load(std::uint8_t* output, std::size_t capacity,
                              std::size_t& size) {
    size = 0u;
    if (!output) return false;
    std::uint8_t records[2][slotBytes]{};
    SlotInfo slots[2]{};
    if (!inspect(0u, records[0], slots[0]) || !inspect(1u, records[1], slots[1])) {
        return false;
    }
    auto selected = slots[1].valid && (!slots[0].valid ||
        static_cast<std::int32_t>(slots[1].generation - slots[0].generation) > 0)
        ? 1u : 0u;
    if (!slots[selected].valid || slots[selected].payloadSize > capacity) return false;
    size = slots[selected].payloadSize;
    std::memcpy(output, records[selected] + payloadOffset, size);
    return true;
}

bool RedundantBlobStore::commit(const std::uint8_t* data, std::size_t size) {
    if (!data || size == 0u || size > maxPayloadBytes || !layoutValid()) return false;
    std::uint8_t records[2][slotBytes]{};
    SlotInfo slots[2]{};
    if (!inspect(0u, records[0], slots[0]) || !inspect(1u, records[1], slots[1])) {
        return false;
    }
    const auto newest = slots[1].valid && (!slots[0].valid ||
        static_cast<std::int32_t>(slots[1].generation - slots[0].generation) > 0)
        ? 1u : 0u;
    const auto haveNewest = slots[newest].valid;
    const auto target = static_cast<std::uint8_t>(haveNewest ? 1u - newest : 0u);
    const auto generation = haveNewest ? slots[newest].generation + 1u : 1u;

    std::uint8_t record[slotBytes]{};
    write32(record, recordMagic);
    write16(record + 4u, recordVersion);
    write16(record + 6u, static_cast<std::uint16_t>(size));
    write32(record + 8u, generation);
    write32(record + 12u, crc32(data, size));
    std::memcpy(record + payloadOffset, data, size);
    const auto address = offset_ + static_cast<std::uint32_t>(target * slotBytes);
    if (!storage_.write(address, record, sizeof record)) return false;
    std::uint8_t marker[4]{};
    write32(marker, commitMagic);
    if (!storage_.write(address + commitOffset, marker, sizeof marker)) return false;

    SlotInfo verified{};
    return inspect(target, record, verified) && verified.valid &&
           verified.generation == generation && verified.payloadSize == size &&
           std::memcmp(record + payloadOffset, data, size) == 0;
}

} // namespace slstm32::storage
