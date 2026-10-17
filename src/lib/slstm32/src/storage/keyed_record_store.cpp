#include "slstm32/storage/keyed_record_store.hpp"
#include <cstring>

namespace slstm32::storage {

namespace {

// Key id 0 marks an erased slot.
void putKey(std::uint8_t* out, const KeyedRecordStore::Key& key) {
    for (std::size_t index = 0u; index < 8u; ++index) out[index] = static_cast<std::uint8_t>(key.id >> (8u * index));
    out[8] = key.tag;
}

KeyedRecordStore::Key getKey(const std::uint8_t* in) {
    KeyedRecordStore::Key key{};
    for (std::size_t index = 0u; index < 8u; ++index) key.id |= static_cast<std::uint64_t>(in[index]) << (8u * index);
    key.tag = in[8];
    return key;
}

} // namespace

RedundantBlobStore KeyedRecordStore::slot(std::size_t index) {
    return RedundantBlobStore{storage_, offset_ + static_cast<std::uint32_t>(index * slotBytes), slotBytes};
}

int KeyedRecordStore::find(const Key& key) const {
    for (std::size_t index = 0u; index < slots_; ++index) {
        if (keys_[index].id != 0u && keys_[index] == key) return static_cast<int>(index);
    }
    return -1;
}

bool KeyedRecordStore::scan() {
    for (std::size_t index = 0u; index < slots_; ++index) {
        std::uint8_t record[RedundantBlobStore::maxPayloadBytes]{};
        std::size_t size = 0u;
        keys_[index] = {};
        // A slot that was never written has no valid record; that is not a
        // storage failure, so only the storage reads below can fail scan().
        if (!slot(index).load(record, sizeof record, size)) continue;
        if (size < keyBytes) continue;
        keys_[index] = getKey(record);
    }
    // RedundantBlobStore::load() does not tell an empty slot from a failed
    // read, so check that the storage answers at all.
    std::uint8_t probe = 0u;
    return slots_ == 0u || storage_.read(offset_, &probe, 1u);
}

bool KeyedRecordStore::load(const Key& key, std::uint8_t* data, std::size_t capacity, std::size_t& size) {
    const int index = find(key);
    if (index < 0) return false;
    std::uint8_t record[RedundantBlobStore::maxPayloadBytes]{};
    std::size_t recordSize = 0u;
    if (!slot(static_cast<std::size_t>(index)).load(record, sizeof record, recordSize) || recordSize < keyBytes ||
        !(getKey(record) == key)) {
        return false;
    }
    size = recordSize - keyBytes;
    if (size > capacity) return false;
    std::memcpy(data, record + keyBytes, size);
    return true;
}

bool KeyedRecordStore::save(const Key& key, const std::uint8_t* data, std::size_t size) {
    if (key.id == 0u || size > maxPayloadBytes) return false;
    int index = find(key);
    if (index < 0) {
        for (std::size_t candidate = 0u; candidate < slots_; ++candidate) {
            if (keys_[candidate].id == 0u) {
                index = static_cast<int>(candidate);
                break;
            }
        }
    }
    if (index < 0) return false;
    std::uint8_t record[RedundantBlobStore::maxPayloadBytes]{};
    putKey(record, key);
    std::memcpy(record + keyBytes, data, size);
    if (!slot(static_cast<std::size_t>(index)).commit(record, keyBytes + size)) return false;
    keys_[static_cast<std::size_t>(index)] = key;
    return true;
}

bool KeyedRecordStore::erase(const Key& key) {
    const int index = find(key);
    if (index < 0) return true;
    std::uint8_t record[keyBytes]{};
    if (!slot(static_cast<std::size_t>(index)).commit(record, sizeof record)) return false;
    keys_[static_cast<std::size_t>(index)] = {};
    return true;
}

std::size_t KeyedRecordStore::used() const {
    std::size_t count = 0u;
    for (std::size_t index = 0u; index < slots_; ++index) count += keys_[index].id != 0u ? 1u : 0u;
    return count;
}

} // namespace slstm32::storage
