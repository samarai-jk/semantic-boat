#include "slstm32/drivers/i2c_bus.hpp"

namespace slstm32::drivers {

std::size_t I2cAddressSet::size() const {
    std::size_t count = 0u;
    for (auto word : words_) {
        for (; word != 0u; word &= word - 1u) ++count;
    }
    return count;
}

I2cAddressSet scanI2c(I2cBus& bus) {
    I2cAddressSet found;
    for (auto address = I2cAddressSet::firstAddress; address <= I2cAddressSet::lastAddress;
         ++address) {
        if (bus.probe(address)) found.insert(address);
    }
    return found;
}

} // namespace slstm32::drivers
