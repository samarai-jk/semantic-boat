#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/led.hpp"

struct __UART_HandleTypeDef;
typedef struct __UART_HandleTypeDef UART_HandleTypeDef;

namespace remote_a {

class Rs485Bridge final : public slstm32::Driver {
public:
    explicit Rs485Bridge(slstm32::drivers::LedDriver& activityLed) : activityLed_(activityLed) {}
    bool init() override;
    void run() override;
    void onReceive(UART_HandleTypeDef* uart, std::uint16_t size);
    void onError(UART_HandleTypeDef* uart);

private:
    void startReceive();
    void push(const std::uint8_t* data, std::uint16_t size);
    std::uint16_t pop(std::uint8_t* data, std::uint16_t capacity);

    static constexpr std::size_t ringCapacity = 1024u;
    slstm32::drivers::LedDriver& activityLed_;
    std::array<std::uint8_t, 64> receiveBuffer_{};
    std::array<std::uint8_t, ringCapacity> ring_{};
    volatile std::uint16_t head_{};
    volatile std::uint16_t tail_{};
    volatile bool activity_{};
};

} // namespace remote_a
