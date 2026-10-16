#include "slstm32/drivers/at24c256.hpp"
#include <cstring>

namespace slstm32::drivers {

bool At24c256I2cBinding::read(void* context, std::uint16_t address, std::uint8_t* data,
                              std::size_t size) {
    auto& self = *static_cast<At24c256I2cBinding*>(context);
    const std::uint8_t prefix[2]{static_cast<std::uint8_t>(address >> 8u),
                                 static_cast<std::uint8_t>(address)};
    if (self.bus_.readFrom(self.address_, prefix, sizeof prefix, data, size)) return true;
    self.lastError_ = Error::readFailed;
    return false;
}

bool At24c256I2cBinding::writePage(void* context, std::uint16_t address,
                                   const std::uint8_t* data, std::size_t size) {
    auto& self = *static_cast<At24c256I2cBinding*>(context);
    if (size > At24c256::pageSize) {
        self.lastError_ = Error::writeFailed;
        return false;
    }
    std::uint8_t frame[2u + At24c256::pageSize];
    frame[0] = static_cast<std::uint8_t>(address >> 8u);
    frame[1] = static_cast<std::uint8_t>(address);
    std::memcpy(frame + 2u, data, size);
    if (!self.bus_.write(self.address_, frame, 2u + size)) {
        self.lastError_ = Error::writeFailed;
        return false;
    }
    if (!self.waitForWriteCycle()) {
        self.lastError_ = Error::writeCycleTimeout;
        return false;
    }
    if (!self.verifyWrites_) return true;

    std::uint8_t verification[At24c256::pageSize];
    if (!self.bus_.readFrom(self.address_, frame, 2u, verification, size)) {
        self.lastError_ = Error::verifyReadFailed;
        return false;
    }
    if (std::memcmp(data, verification, size) != 0) {
        self.lastError_ = Error::verifyMismatch;
        return false;
    }
    return true;
}

bool At24c256I2cBinding::waitForWriteCycle() {
    // Poll against elapsed time rather than a probe count: fast NACKs can
    // exhaust any fixed count well inside the 5 ms write time.
    if (!runtime_.millis) return false;
    const auto startedAt = runtime_.millis();
    do {
        if (bus_.probe(address_)) return true;
    } while (static_cast<std::uint32_t>(runtime_.millis() - startedAt) <= writeCycleTimeoutMs);
    return bus_.probe(address_);
}

bool At24c256::read(std::uint32_t address, std::uint8_t* data, std::size_t size) {
    if (!hardware_.read || (!data && size != 0u) || address > storageSize ||
        size > storageSize - address) return false;
    return size == 0u || hardware_.read(hardware_.context, static_cast<std::uint16_t>(address), data, size);
}

bool At24c256::write(std::uint32_t address, const std::uint8_t* data, std::size_t size) {
    if (!hardware_.writePage || (!data && size != 0u) || address > storageSize ||
        size > storageSize - address) return false;
    while (size != 0u) {
        const auto pageRemaining = pageSize - address % pageSize;
        const auto amount = size < pageRemaining ? size : pageRemaining;
        if (!hardware_.writePage(hardware_.context, static_cast<std::uint16_t>(address), data, amount)) {
            return false;
        }
        address += amount;
        data += amount;
        size -= amount;
    }
    return true;
}

} // namespace slstm32::drivers
