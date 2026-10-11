#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/storage/byte_storage.hpp"

namespace slstm32::drivers {

struct At24c256Hardware {
    void* context{};
    bool (*read)(void* context, std::uint16_t address, std::uint8_t* data, std::size_t size){};
    bool (*writePage)(void* context, std::uint16_t address,
                      const std::uint8_t* data, std::size_t size){};
};

class At24c256 final : public storage::ByteStorage {
public:
    static constexpr std::size_t storageSize = 32u * 1024u;
    static constexpr std::size_t pageSize = 64u;

    explicit At24c256(At24c256Hardware hardware) : hardware_(hardware) {}
    std::size_t capacity() const override { return storageSize; }
    bool read(std::uint32_t address, std::uint8_t* data, std::size_t size) override;
    bool write(std::uint32_t address, const std::uint8_t* data, std::size_t size) override;

private:
    At24c256Hardware hardware_;
};

} // namespace slstm32::drivers
