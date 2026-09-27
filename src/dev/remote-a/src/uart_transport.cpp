#include "uart_transport.hpp"
extern "C" {
#include "main.h"
}

namespace remote_a {
namespace {

constexpr std::uint32_t activityLedPulseMs = 1u;

} // namespace

bool UartTransport::init() {
    if (!uart_) return false;
    HAL_NVIC_SetPriority(static_cast<IRQn_Type>(irq_), 6u, 0u);
    HAL_NVIC_EnableIRQ(static_cast<IRQn_Type>(irq_));
    startReceive();
    return true;
}

void UartTransport::startReceive() {
    if (!sleeping_) {
        (void)HAL_UARTEx_ReceiveToIdle_IT(
            uart_, receiveBuffer_.data(), static_cast<std::uint16_t>(receiveBuffer_.size()));
    }
}

void UartTransport::setClock(bool enabled) {
    if (uart_->Instance == USART1) {
        if (enabled) __HAL_RCC_USART1_CLK_ENABLE();
        else __HAL_RCC_USART1_CLK_DISABLE();
    } else if (uart_->Instance == USART2) {
        if (enabled) __HAL_RCC_USART2_CLK_ENABLE();
        else __HAL_RCC_USART2_CLK_DISABLE();
    }
}

void UartTransport::setSleeping(bool sleeping) {
    if (sleeping == sleeping_) return;
    sleeping_ = sleeping;
    const auto irq = static_cast<IRQn_Type>(irq_);
    if (sleeping) {
        HAL_NVIC_DisableIRQ(irq);
        (void)HAL_UART_Abort(uart_);
        HAL_NVIC_ClearPendingIRQ(irq);
        __HAL_UART_DISABLE(uart_);
        setClock(false);
        return;
    }

    setClock(true);
    __HAL_UART_ENABLE(uart_);
    HAL_NVIC_ClearPendingIRQ(irq);
    startReceive();
    HAL_NVIC_EnableIRQ(irq);
}

void UartTransport::setActivityIndicatorEnabled(bool enabled) {
    activityIndicatorEnabled_ = enabled;
    if (!enabled) {
        activity_ = false;
        activityLed_.off();
    }
}

std::size_t UartTransport::read(std::uint8_t* data, std::size_t capacity) {
    if (!data) return 0u;
    std::size_t count{};
    while (tail_ != head_ && count < capacity) {
        data[count++] = ring_[tail_];
        tail_ = static_cast<std::uint16_t>((tail_ + 1u) % ringCapacity);
    }
    return count;
}

bool UartTransport::write(const std::uint8_t* data, std::size_t size) {
    if (sleeping_ || !data || size == 0u || size > 0xffffu) return false;
    const auto result = HAL_UART_Transmit(
        uart_, const_cast<std::uint8_t*>(data), static_cast<std::uint16_t>(size), 100u);
    return result == HAL_OK;
}

void UartTransport::onReceive(UART_HandleTypeDef* uart, std::uint16_t size) {
    if (sleeping_ || uart != uart_) return;
    for (std::uint16_t index = 0u; index < size; ++index) {
        const auto next = static_cast<std::uint16_t>((head_ + 1u) % ringCapacity);
        if (next == tail_) {
            ++droppedBytes_;
            continue;
        }
        ring_[head_] = receiveBuffer_[index];
        head_ = next;
    }
    activity_ = activity_ || size != 0u;
    startReceive();
}

void UartTransport::onError(UART_HandleTypeDef* uart) {
    if (!sleeping_ && uart == uart_) startReceive();
}

void UartTransport::run() {
    if (!activity_) return;
    activity_ = false;
    if (activityIndicatorEnabled_) activityLed_.pulse(activityLedPulseMs);
}

} // namespace remote_a
