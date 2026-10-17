#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/storage/byte_storage.hpp"
#include "slstm32/storage/redundant_blob_store.hpp"

namespace slstm32::storage {

// Small records addressed by a key instead of a position, for settings that
// belong to something with a stable identity (a sensor's ROM code, an I2C
// address, an ADC input). Each key occupies one independent slot (a
// RedundantBlobStore: two alternating 64-byte records with CRC), so:
// - adding a key takes the first free slot and leaves every other record as
//   it is: configured devices keep their settings when hardware is added;
// - updating one record never touches another and survives power loss;
// - a key whose hardware is removed keeps its slot until erase(), so its
//   settings return with the hardware.
// scan() reads all slots once (about 10 ms per slot on a 100 kHz I2C EEPROM)
// and keeps the keys in a caller-provided array (16 bytes of RAM per slot).
class KeyedRecordStore {
public:
    struct Key {
        std::uint64_t id{};
        std::uint8_t tag{};
        bool operator==(const Key& other) const { return id == other.id && tag == other.tag; }
    };

    static constexpr std::size_t slotBytes = RedundantBlobStore::requiredBytes;
    static constexpr std::size_t keyBytes = 9u;
    static constexpr std::size_t maxPayloadBytes = RedundantBlobStore::maxPayloadBytes - keyBytes;

    template <std::size_t Slots>
    KeyedRecordStore(ByteStorage& storage, std::uint32_t offset, Key (&keys)[Slots])
        : storage_(storage), offset_(offset), keys_(keys), slots_(Slots) {}

    // Reads the keys of all slots. Returns false if the storage failed.
    bool scan();
    bool load(const Key& key, std::uint8_t* data, std::size_t capacity, std::size_t& size);
    // Updates the key's slot or takes a free one. False when the store is
    // full, the payload is too large, or the storage failed.
    bool save(const Key& key, const std::uint8_t* data, std::size_t size);
    bool erase(const Key& key);
    std::size_t slots() const { return slots_; }
    std::size_t used() const;
    static constexpr std::size_t requiredBytes(std::size_t slots) { return slots * slotBytes; }

private:
    RedundantBlobStore slot(std::size_t index);
    int find(const Key& key) const;

    ByteStorage& storage_;
    std::uint32_t offset_{};
    Key* keys_{}; // id 0: free slot
    std::size_t slots_{};
};

} // namespace slstm32::storage
