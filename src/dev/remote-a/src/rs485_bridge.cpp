#include "rs485_bridge.hpp"
extern "C" {
#include "main.h"
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
}

namespace remote_a {

bool Rs485Bridge::init() {
    HAL_NVIC_SetPriority(USART2_IRQn, 6u, 0u);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    startReceive();
    return true;
}

void Rs485Bridge::startReceive() {
    if (sleeping_) return;
    (void)HAL_UARTEx_ReceiveToIdle_IT(&huart2, receiveBuffer_.data(),
                                     static_cast<std::uint16_t>(receiveBuffer_.size()));
}

void Rs485Bridge::setSleeping(bool sleeping) {
    if (sleeping == sleeping_) return;
    sleeping_ = sleeping;
    if (sleeping) {
        HAL_NVIC_DisableIRQ(USART2_IRQn);
        (void)HAL_UART_AbortReceive(&huart2);
        HAL_NVIC_ClearPendingIRQ(USART2_IRQn);
        __HAL_UART_DISABLE(&huart2);
        __HAL_RCC_USART2_CLK_DISABLE();
        return;
    }

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_UART_ENABLE(&huart2);
    HAL_NVIC_ClearPendingIRQ(USART2_IRQn);
    startReceive();
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

void Rs485Bridge::push(const std::uint8_t* data, std::uint16_t size) {
    for (std::uint16_t i = 0; i < size; ++i) {
        const auto next = static_cast<std::uint16_t>((head_ + 1u) % ringCapacity);
        if (next == tail_) break;
        ring_[head_] = data[i];
        head_ = next;
    }
}

std::uint16_t Rs485Bridge::pop(std::uint8_t* data, std::uint16_t capacity) {
    std::uint16_t count{};
    while (tail_ != head_ && count < capacity) {
        data[count++] = ring_[tail_];
        tail_ = static_cast<std::uint16_t>((tail_ + 1u) % ringCapacity);
    }
    return count;
}

void Rs485Bridge::onReceive(UART_HandleTypeDef* uart, std::uint16_t size) {
    if (sleeping_ || !uart || uart->Instance != USART2) return;
    push(receiveBuffer_.data(), size);
    activity_ = size != 0u;
    startReceive();
}

void Rs485Bridge::onError(UART_HandleTypeDef* uart) {
    if (!sleeping_ && uart && uart->Instance == USART2) startReceive();
}

void Rs485Bridge::run() {
    if (sleeping_) return;
    if (activity_) {
        activity_ = false;
        activityLed_.pulse(50u);
    }
    std::uint8_t chunk[64]{};
    const auto count = pop(chunk, sizeof chunk);
    if (count != 0u) (void)HAL_UART_Transmit(&huart1, chunk, count, 100u);
}

} // namespace remote_a
