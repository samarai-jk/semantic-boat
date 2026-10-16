#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Terminal error state for the CubeMX Error_Handler and fault handlers.
 * Writes the reason to the ST-Link VCP if USART1 is running and then blinks
 * the yellow CAN LED forever. Works before SysTick and GPIO initialization. */
void can_mcu_fatal_error(const char* reason) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
