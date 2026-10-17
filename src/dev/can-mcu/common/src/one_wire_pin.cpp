#include "can_mcu/one_wire_pin.hpp"
#include "can_mcu/board.hpp"
extern "C" {
#include "main.h"
}

namespace can_mcu {

namespace {

GPIO_TypeDef* gpio(GpioPort port) {
    switch (port) {
    case GpioPort::a: return GPIOA;
    case GpioPort::b: return GPIOB;
    case GpioPort::c: return GPIOC;
    case GpioPort::h: return GPIOH;
    }
    return GPIOA;
}

} // namespace

bool OneWirePin::configure(void* context) {
    const auto& self = *static_cast<OneWirePin*>(context);
    switch (self.port_) {
    case GpioPort::a: __HAL_RCC_GPIOA_CLK_ENABLE(); break;
    case GpioPort::b: __HAL_RCC_GPIOB_CLK_ENABLE(); break;
    case GpioPort::c: __HAL_RCC_GPIOC_CLK_ENABLE(); break;
    case GpioPort::h: __HAL_RCC_GPIOH_CLK_ENABLE(); break;
    }
    auto* port = gpio(self.port_);
    port->BSRR = self.mask_; // released before the output is enabled
    GPIO_InitTypeDef init{};
    init.Pin = self.mask_;
    init.Mode = GPIO_MODE_OUTPUT_OD;
    init.Pull = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &init);
    return true;
}

void OneWirePin::driveLow(void* context) {
    const auto& self = *static_cast<OneWirePin*>(context);
    gpio(self.port_)->BRR = self.mask_;
}

void OneWirePin::release(void* context) {
    const auto& self = *static_cast<OneWirePin*>(context);
    gpio(self.port_)->BSRR = self.mask_;
}

bool OneWirePin::read(void* context) {
    const auto& self = *static_cast<OneWirePin*>(context);
    return (gpio(self.port_)->IDR & self.mask_) != 0u;
}

void OneWirePin::delay(std::uint32_t microseconds) {
    delayMicroseconds(microseconds);
}

} // namespace can_mcu
