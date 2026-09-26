#pragma once

#include <cstddef>
#include <cstdint>

namespace slstm32::storage {

class ByteStorage {
public:
    virtual ~ByteStorage() = default;
    virtual std::size_t capacity() const = 0;
    virtual bool read(std::uint32_t address, std::uint8_t* data, std::size_t size) = 0;
    virtual bool write(std::uint32_t address, const std::uint8_t* data, std::size_t size) = 0;
};

} // namespace slstm32::storage
