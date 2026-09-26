#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/led.hpp"

struct __UART_HandleTypeDef;
typedef struct __UART_HandleTypeDef UART_HandleTypeDef;

namespace remote_a {

class UartTransport final : public slstm32::Driver {
public:
    UartTransport(UART_HandleTypeDef* uart, std::int32_t irq,
                  slstm32::drivers::LedDriver& activityLed)
        : uart_(uart), irq_(irq), activityLed_(activityLed) {}

    bool init() override;
    void run() override;
    void setSleeping(bool sleeping);
    std::size_t read(std::uint8_t* data, std::size_t capacity);
    bool write(const std::uint8_t* data, std::size_t size);
    void onReceive(UART_HandleTypeDef* uart, std::uint16_t size);
    void onError(UART_HandleTypeDef* uart);
    std::uint32_t droppedBytes() const { return droppedBytes_; }

private:
    void startReceive();
    void setClock(bool enabled);

    static constexpr std::size_t receiveChunkSize = 64u;
    static constexpr std::size_t ringCapacity = 512u;
    UART_HandleTypeDef* uart_{};
    std::int32_t irq_{};
    slstm32::drivers::LedDriver& activityLed_;
    std::array<std::uint8_t, receiveChunkSize> receiveBuffer_{};
    std::array<std::uint8_t, ringCapacity> ring_{};
    volatile std::uint16_t head_{};
    volatile std::uint16_t tail_{};
    volatile std::uint32_t droppedBytes_{};
    volatile bool activity_{};
    bool sleeping_{};
};

} // namespace remote_a
