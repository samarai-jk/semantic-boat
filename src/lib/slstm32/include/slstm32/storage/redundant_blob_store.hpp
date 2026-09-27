#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/storage/byte_storage.hpp"

namespace slstm32::storage {

// A small power-loss-tolerant blob store. Two 64-byte slots alternate on each
// commit; the commit marker is written last so the older generation remains
// usable if power is removed during a write.
class RedundantBlobStore {
public:
    static constexpr std::size_t slotBytes = 64u;
    static constexpr std::size_t requiredBytes = 2u * slotBytes;
    static constexpr std::size_t maxPayloadBytes = 40u;

    RedundantBlobStore(ByteStorage& storage, std::uint32_t offset,
                       std::size_t availableBytes)
        : storage_(storage), offset_(offset), availableBytes_(availableBytes) {}

    bool load(std::uint8_t* output, std::size_t capacity, std::size_t& size);
    bool commit(const std::uint8_t* data, std::size_t size);

private:
    struct SlotInfo {
        std::uint32_t generation{};
        std::uint16_t payloadSize{};
        std::uint8_t slot{};
        bool valid{};
    };

    bool inspect(std::uint8_t slot, std::uint8_t* record, SlotInfo& info);
    bool layoutValid() const;

    ByteStorage& storage_;
    std::uint32_t offset_{};
    std::size_t availableBytes_{};
};

} // namespace slstm32::storage
