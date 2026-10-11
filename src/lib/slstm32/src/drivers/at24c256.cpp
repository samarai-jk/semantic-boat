#include "slstm32/drivers/at24c256.hpp"

namespace slstm32::drivers {

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
