// SWO + UART trace utility implementation
#include "lib.hpp"

// Include CMSIS core via HAL umbrella so we get ITM/CoreDebug definitions and HAL UART
#include "main.h"
#include <stdint.h>
#include <string>
#include <cstring>
#include <cstdarg>
#include <cstdio>

// --- UART2 (PA2/PA3) minimal bring-up for trace mirroring ---
// Notes:
// - Pins PA2/PA3 are configured to AF7 USART2 in MX_GPIO_Init() (main.c)
// - We enable the USART2 clock and init here lazily on first use
// - HAL_UART_MspInit is left empty (default weak), since GPIOs and clocks are handled here
static UART_HandleTypeDef s_uart2;
static bool s_uart2_ready = false;

static void uart2_init_if_needed()
{
    if (s_uart2_ready) return;
    __HAL_RCC_USART2_CLK_ENABLE();
    s_uart2.Instance = USART2;
    s_uart2.Init.BaudRate = 115200;
    s_uart2.Init.WordLength = UART_WORDLENGTH_8B;
    s_uart2.Init.StopBits = UART_STOPBITS_1;
    s_uart2.Init.Parity = UART_PARITY_NONE;
    s_uart2.Init.Mode = UART_MODE_TX_RX;
    s_uart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_uart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&s_uart2) == HAL_OK)
    {
        s_uart2_ready = true;
    }
}

static inline void uart2_putc(uint8_t ch)
{
    uart2_init_if_needed();
    if (!s_uart2_ready) return;
    (void)HAL_UART_Transmit(&s_uart2, &ch, 1U, 1U);
}

static inline void uart2_write(const char* buf, size_t len)
{
    if (buf == nullptr || len == 0) return;
    uart2_init_if_needed();
    if (!s_uart2_ready) return;
    (void)HAL_UART_Transmit(&s_uart2, reinterpret_cast<uint8_t*>(const_cast<char*>(buf)), (uint16_t)len, 10U);
}

// Local helper to emit a byte on ITM port 0 without relying on ITM_SendChar symbol
static inline void swo_putc(uint8_t ch)
{
    // Check ITM and stimulus port 0 are enabled
    if ((ITM->TCR & ITM_TCR_ITMENA_Msk) == 0U)
        return;
    if ((ITM->TER & 1U) == 0U)
        return;
    // Wait until stimulus port 0 is ready, then write
    while (ITM->PORT[0].u32 == 0U) { }
    ITM->PORT[0].u8 = ch;
}

static inline const char* level_to_str(log_level_t level)
{
    switch (level)
    {
        case LOG_INFO:    return "INFO";
        case LOG_VERBOSE: return "VERB";
        case LOG_ERROR:   return "ERROR";
        case LOG_FATAL:   return "FATAL";
        case LOG_WARNING: return "WARNING";
        case LOG_BUGFIX:  return "BUGFIX";
        default:          return "INFO";
    }
}

extern "C" void swo_trace_line_level(log_level_t level, const char *s)
{
    
    if (!s) 
        return;
    
    uint32_t ms = HAL_GetTick();
    std::string line;
    line.reserve(32 + (s ? strlen(s) : 0));
    line += std::to_string(ms);
    line.push_back(' ');
    line.push_back('[');
    line += level_to_str(level);
    line.push_back(']');
    line.push_back(' ');
    line += s;

    // Emit to SWO (LF)
    for (char c : line) { swo_putc((uint8_t)c); }
    swo_putc((uint8_t)'\n');

    // Emit to UART (CRLF)
    uart2_write(line.c_str(), line.size());
    static const char crlf[] = "\r\n";
    uart2_write(crlf, 2);
    
}
 
extern "C" void swo_trace_linef(log_level_t level, const char *fmt, ...)
{
    if (!fmt) 
        return;
    
    // Format the message into a buffer
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    
    // Now call the regular line-level function
    swo_trace_line_level(level, buf);
}
