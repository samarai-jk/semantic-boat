// EXTI vectors for every can-mcu device. Each handler forwards all lines of
// its group to HAL_GPIO_EXTI_IRQHandler(), which clears the pending flag and
// calls the device's HAL_GPIO_EXTI_Callback(pin). Do not enable EXTI
// interrupts in CubeMX; duplicate handlers would fail to link.

#include <cstdint>
extern "C" {
#include "main.h"
}

namespace {

void dispatch(unsigned first, unsigned last) {
    for (auto line = first; line <= last; ++line) {
        HAL_GPIO_EXTI_IRQHandler(static_cast<std::uint16_t>(1u << line));
    }
}

} // namespace

extern "C" {
void EXTI0_IRQHandler(void) { dispatch(0u, 0u); }
void EXTI1_IRQHandler(void) { dispatch(1u, 1u); }
void EXTI2_IRQHandler(void) { dispatch(2u, 2u); }
void EXTI3_IRQHandler(void) { dispatch(3u, 3u); }
void EXTI4_IRQHandler(void) { dispatch(4u, 4u); }
void EXTI9_5_IRQHandler(void) { dispatch(5u, 9u); }
void EXTI15_10_IRQHandler(void) { dispatch(10u, 15u); }
}
