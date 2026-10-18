// Startup of the can-mcu bootloader: vector table (core exceptions only; the
// bootloader polls and enables no peripheral interrupt) and reset handler.
#include <cstdint>
#include "stm32l4xx.h"

extern "C" {
extern std::uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
void Reset_Handler();
void SysTick_Handler();
}

void bootMain(); // main.cpp

namespace {

[[noreturn]] void halt() {
    // A fault in the bootloader: reset and try again.
    NVIC_SystemReset();
    while (true) {
    }
}

} // namespace

extern "C" void Reset_Handler() {
    for (auto *source = &_sidata, *target = &_sdata; target < &_edata;) *target++ = *source++;
    for (auto* target = &_sbss; target < &_ebss;) *target++ = 0u;
    SCB->CPACR |= (3u << 20u) | (3u << 22u); // FPU, as the C++ ABI may use it
    bootMain();
    halt();
}

extern "C" void Fault_Handler() { halt(); }

using Vector = void (*)();
[[gnu::section(".isr_vector"), gnu::used]] const Vector vectors[16]{
    reinterpret_cast<Vector>(&_estack), Reset_Handler, Fault_Handler, Fault_Handler, Fault_Handler, Fault_Handler,
    Fault_Handler, nullptr, nullptr, nullptr, nullptr, Fault_Handler, Fault_Handler, nullptr, Fault_Handler,
    SysTick_Handler,
};
