#pragma once

#include "slstm32/epd/transport.hpp"

namespace remote_a {

class HalEpdTransport final : public slstm32::epd::Transport {
public:
    bool writeCommand(std::uint8_t command) override;
    bool writeData(const std::uint8_t* data, std::size_t size) override;
    void reset() override;
    void power(bool enabled) override;
    bool busy() const override;
    void delayMs(std::uint32_t duration) override;
    std::uint32_t millis() const override;
};

} // namespace remote_a
