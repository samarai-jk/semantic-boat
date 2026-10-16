#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/drivers/i2c_bus.hpp"
#include "slstm32/runtime.hpp"
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

// At24c256Hardware on an I2cBus: 16-bit memory addressing, page writes, and
// acknowledge polling for the internal write cycle (the part ignores its
// address for up to 5 ms after a write). Written pages are read back and
// compared unless verification is disabled. The binding must outlive the
// At24c256 that uses it.
class At24c256I2cBinding {
public:
    enum class Error : std::uint8_t {
        none,
        readFailed,
        writeFailed,
        writeCycleTimeout,
        verifyReadFailed,
        verifyMismatch,
    };

    At24c256I2cBinding(Runtime runtime, I2cBus& bus, std::uint8_t address,
                       bool verifyWrites = true)
        : runtime_(runtime), bus_(bus), address_(address), verifyWrites_(verifyWrites) {}

    At24c256Hardware hardware() { return {this, &read, &writePage}; }
    bool present() { return bus_.probe(address_); }
    Error lastError() const { return lastError_; }
    void clearError() { lastError_ = Error::none; }

private:
    static constexpr std::uint32_t writeCycleTimeoutMs = 10u;

    static bool read(void* context, std::uint16_t address, std::uint8_t* data, std::size_t size);
    static bool writePage(void* context, std::uint16_t address, const std::uint8_t* data,
                          std::size_t size);
    bool waitForWriteCycle();

    Runtime runtime_;
    I2cBus& bus_;
    std::uint8_t address_{};
    bool verifyWrites_{};
    Error lastError_{Error::none};
};

} // namespace slstm32::drivers
