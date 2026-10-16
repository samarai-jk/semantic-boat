#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace slstm32::drivers {

// Blocking I2C master bus with 7-bit addresses. Implementations bind a
// concrete peripheral, enforce their own timeouts, and recover a stuck bus.
// Transfers block the cooperative loop for their duration; keep them short
// (registers and small blocks) and schedule larger work incrementally.
class I2cBus {
public:
    virtual ~I2cBus() = default;
    // True when a device acknowledges its address.
    virtual bool probe(std::uint8_t address) = 0;
    virtual bool write(std::uint8_t address, const std::uint8_t* data, std::size_t size) = 0;
    virtual bool read(std::uint8_t address, std::uint8_t* data, std::size_t size) = 0;
    // Writes `prefix` (typically a register address of one or two bytes) and
    // reads `size` bytes in one transaction.
    virtual bool readFrom(std::uint8_t address, const std::uint8_t* prefix,
                          std::size_t prefixSize, std::uint8_t* data, std::size_t size) = 0;

    bool readRegister(std::uint8_t address, std::uint8_t reg, std::uint8_t* data,
                      std::size_t size) {
        return readFrom(address, &reg, 1u, data, size);
    }
    bool writeRegister(std::uint8_t address, std::uint8_t reg, std::uint8_t value) {
        const std::uint8_t frame[2]{reg, value};
        return write(address, frame, sizeof frame);
    }
};

// Set of 7-bit addresses that acknowledged during a scan.
class I2cAddressSet {
public:
    static constexpr std::uint8_t firstAddress = 0x08u;
    static constexpr std::uint8_t lastAddress = 0x77u;

    bool contains(std::uint8_t address) const {
        return address < 128u && (words_[address / 32u] & (1u << (address % 32u))) != 0u;
    }
    void insert(std::uint8_t address) {
        if (address < 128u) words_[address / 32u] |= 1u << (address % 32u);
    }
    std::size_t size() const;
    bool operator==(const I2cAddressSet& other) const { return words_ == other.words_; }
    bool operator!=(const I2cAddressSet& other) const { return !(*this == other); }

private:
    std::array<std::uint32_t, 4> words_{};
};

// Probes every non-reserved 7-bit address (0x08..0x77). Blocks for roughly
// 112 address phases.
I2cAddressSet scanI2c(I2cBus& bus);

} // namespace slstm32::drivers
