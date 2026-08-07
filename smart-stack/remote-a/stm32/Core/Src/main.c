/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c2;

RTC_HandleTypeDef hrtc;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim15;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C2_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM15_Init(void);
static void MX_RTC_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// RS485/USART2 settings: 115200 baud, 8 data bits, no parity, 1 stop bit (8N1)
// RX is interrupt-driven (ReceiveToIdle). Incoming bytes are forwarded to USART1 (debug).

#define RS485_RX_TMP_SIZE 64u
#define RS485_RING_SIZE 1024u

static uint8_t rs485_rx_tmp[RS485_RX_TMP_SIZE];
static volatile uint16_t rs485_rb_head = 0u;
static volatile uint16_t rs485_rb_tail = 0u;
static uint8_t rs485_rb[RS485_RING_SIZE];

static volatile uint8_t rs485_rx_activity = 0u;
static uint32_t rs485_led_off_ms = 0u;

static void RS485_RingPush(const uint8_t *data, uint16_t len)
{
  if (data == NULL || len == 0u)
  {
    return;
  }

  for (uint16_t i = 0; i < len; i++)
  {
    uint16_t next = (uint16_t)((rs485_rb_head + 1u) % RS485_RING_SIZE);
    if (next == rs485_rb_tail)
    {
      break;
    }
    rs485_rb[rs485_rb_head] = data[i];
    rs485_rb_head = next;
  }
}

static uint16_t RS485_RingPop(uint8_t *out, uint16_t max_len)
{
  if (out == NULL || max_len == 0u)
  {
    return 0u;
  }

  uint16_t n = 0u;
  while ((rs485_rb_tail != rs485_rb_head) && (n < max_len))
  {
    out[n++] = rs485_rb[rs485_rb_tail];
    rs485_rb_tail = (uint16_t)((rs485_rb_tail + 1u) % RS485_RING_SIZE);
  }
  return n;
}

static void RS485_RxKick(void)
{
  (void)HAL_UARTEx_ReceiveToIdle_IT(&huart2, rs485_rx_tmp, (uint16_t)sizeof(rs485_rx_tmp));
}

int __io_putchar(int ch)
{
  uint8_t byte = (uint8_t)ch;
  (void)HAL_UART_Transmit(&huart1, &byte, 1u, 100u);
  return ch;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
  if (huart == NULL)
  {
    return;
  }

  if (huart->Instance == USART2)
  {
    if (size > 0u)
    {
      RS485_RingPush(rs485_rx_tmp, size);
      rs485_rx_activity = 1u;
    }
    RS485_RxKick();
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart != NULL && huart->Instance == USART2)
  {
    RS485_RxKick();
  }
}

#define EPD_3IN7_WIDTH_PX (280u)
#define EPD_3IN7_HEIGHT_PX (480u)
#define EPD_3IN7_BYTES_PER_ROW (EPD_3IN7_WIDTH_PX / 8u)
#define EPD_3IN7_BUF_SIZE ((EPD_3IN7_WIDTH_PX * EPD_3IN7_HEIGHT_PX) / 8u)

static const uint8_t epd_lut_1gray_du[105] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x0A, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x05, 0x05, 0x00, 0x05, 0x03, 0x05, 0x05, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x22, 0x22, 0x22, 0x22, 0x22
};

static const uint8_t epd_lut_1gray_a2[105] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x22, 0x22, 0x22, 0x22, 0x22
};

static const uint8_t epd_lut_1gray_gc[105] = {
  0x2A, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x05, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x2A, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x05, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x02, 0x03, 0x0A, 0x00, 0x02, 0x06, 0x0A, 0x05, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x22, 0x22, 0x22, 0x22, 0x22
};

static uint8_t epd_framebuffer[EPD_3IN7_BUF_SIZE];

static inline void EPD_DelayMs(uint32_t ms)
{
  HAL_Delay(ms);
}

static inline void EPD_CS_Set(GPIO_PinState state)
{
  HAL_GPIO_WritePin(DISP_CS_GPIO_Port, DISP_CS_Pin, state);
}

static inline void EPD_DC_Set(GPIO_PinState state)
{
  HAL_GPIO_WritePin(DISP_DC_GPIO_Port, DISP_DC_Pin, state);
}

static inline void EPD_RST_Set(GPIO_PinState state)
{
  HAL_GPIO_WritePin(DISP_RST_GPIO_Port, DISP_RST_Pin, state);
}

static inline void EPD_PWR_Set(GPIO_PinState state)
{
  HAL_GPIO_WritePin(DISP_PWR_GPIO_Port, DISP_PWR_Pin, state);
}

static inline GPIO_PinState EPD_BUSY_Read(void)
{
  return HAL_GPIO_ReadPin(DISP_BUSY_GPIO_Port, DISP_BUSY_Pin);
}

static int EPD_WaitBusyHighClears(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  while (EPD_BUSY_Read() == GPIO_PIN_SET)
  {
    if ((HAL_GetTick() - start) > timeout_ms)
    {
      return -1;
    }
    EPD_DelayMs(5);
  }

  EPD_DelayMs(200);
  return 0;
}

static void EPD_Reset(void)
{
  EPD_RST_Set(GPIO_PIN_SET);
  EPD_DelayMs(100);
  EPD_RST_Set(GPIO_PIN_RESET);
  EPD_DelayMs(100);
  EPD_RST_Set(GPIO_PIN_SET);
  EPD_DelayMs(100);
}

static void EPD_SendCommand(uint8_t cmd)
{
  EPD_DC_Set(GPIO_PIN_RESET);
  EPD_CS_Set(GPIO_PIN_RESET);
  (void)HAL_SPI_Transmit(&hspi1, &cmd, 1u, 1000u);
  EPD_CS_Set(GPIO_PIN_SET);
}

static void EPD_SendDataByte(uint8_t data)
{
  EPD_DC_Set(GPIO_PIN_SET);
  EPD_CS_Set(GPIO_PIN_RESET);
  (void)HAL_SPI_Transmit(&hspi1, &data, 1u, 1000u);
  EPD_CS_Set(GPIO_PIN_SET);
}

static void EPD_SendDataBuffer(const uint8_t *buf, uint32_t len)
{
  if (buf == NULL || len == 0u)
  {
    return;
  }

  EPD_DC_Set(GPIO_PIN_SET);
  EPD_CS_Set(GPIO_PIN_RESET);
  (void)HAL_SPI_Transmit(&hspi1, (uint8_t *)buf, len, 30000u);
  EPD_CS_Set(GPIO_PIN_SET);
}

typedef enum
{
  EPD_LUT_1GRAY_GC = 1,
  EPD_LUT_1GRAY_DU = 2,
  EPD_LUT_1GRAY_A2 = 3,
} epd_lut_t;

static void EPD_LoadLUT(epd_lut_t lut)
{
  EPD_SendCommand(0x32);
  if (lut == EPD_LUT_1GRAY_A2)
  {
    EPD_SendDataBuffer(epd_lut_1gray_a2, (uint32_t)sizeof(epd_lut_1gray_a2));
  }
  else if (lut == EPD_LUT_1GRAY_DU)
  {
    EPD_SendDataBuffer(epd_lut_1gray_du, (uint32_t)sizeof(epd_lut_1gray_du));
  }
  else
  {
    EPD_SendDataBuffer(epd_lut_1gray_gc, (uint32_t)sizeof(epd_lut_1gray_gc));
  }
}

static int EPD_3IN7_Init_1Gray(void)
{
  EPD_PWR_Set(GPIO_PIN_SET);
  EPD_DelayMs(10);
  EPD_CS_Set(GPIO_PIN_SET);
  EPD_DC_Set(GPIO_PIN_RESET);

  EPD_Reset();

  EPD_SendCommand(0x12);
  EPD_DelayMs(300);

  EPD_SendCommand(0x46);
  EPD_SendDataByte(0xF7);
  if (EPD_WaitBusyHighClears(5000u) != 0)
  {
    return -1;
  }
  EPD_SendCommand(0x47);
  EPD_SendDataByte(0xF7);
  if (EPD_WaitBusyHighClears(5000u) != 0)
  {
    return -1;
  }

  EPD_SendCommand(0x01);
  EPD_SendDataByte(0xDF);
  EPD_SendDataByte(0x01);
  EPD_SendDataByte(0x00);

  EPD_SendCommand(0x03);
  EPD_SendDataByte(0x00);

  EPD_SendCommand(0x04);
  EPD_SendDataByte(0x41);
  EPD_SendDataByte(0xA8);
  EPD_SendDataByte(0x32);

  EPD_SendCommand(0x11);
  EPD_SendDataByte(0x03);

  EPD_SendCommand(0x3C);
  EPD_SendDataByte(0x00);

  EPD_SendCommand(0x0C);
  EPD_SendDataByte(0xAE);
  EPD_SendDataByte(0xC7);
  EPD_SendDataByte(0xC3);
  EPD_SendDataByte(0xC0);
  EPD_SendDataByte(0xC0);

  EPD_SendCommand(0x18);
  EPD_SendDataByte(0x80);

  EPD_SendCommand(0x2C);
  EPD_SendDataByte(0x44);

  EPD_SendCommand(0x37);
  EPD_SendDataByte(0x00);
  EPD_SendDataByte(0xFF);
  EPD_SendDataByte(0xFF);
  EPD_SendDataByte(0xFF);
  EPD_SendDataByte(0xFF);
  EPD_SendDataByte(0x4F);
  EPD_SendDataByte(0xFF);
  EPD_SendDataByte(0xFF);
  EPD_SendDataByte(0xFF);
  EPD_SendDataByte(0xFF);

  EPD_SendCommand(0x44);
  EPD_SendDataByte(0x00);
  EPD_SendDataByte(0x00);
  EPD_SendDataByte(0x17);
  EPD_SendDataByte(0x01);

  EPD_SendCommand(0x45);
  EPD_SendDataByte(0x00);
  EPD_SendDataByte(0x00);
  EPD_SendDataByte(0xDF);
  EPD_SendDataByte(0x01);

  EPD_SendCommand(0x22);
  EPD_SendDataByte(0xCF);

  return 0;
}

static int EPD_3IN7_Display1Gray(const uint8_t *image, uint32_t len, epd_lut_t lut)
{
  if (image == NULL || len != EPD_3IN7_BUF_SIZE)
  {
    return -1;
  }

  EPD_SendCommand(0x4E);
  EPD_SendDataByte(0x00);
  EPD_SendDataByte(0x00);
  EPD_SendCommand(0x4F);
  EPD_SendDataByte(0x00);
  EPD_SendDataByte(0x00);

  EPD_SendCommand(0x24);
  EPD_SendDataBuffer(image, len);

  EPD_LoadLUT(lut);

  EPD_SendCommand(0x20);
  return EPD_WaitBusyHighClears(15000u);
}

static inline void EPD_3IN7_SetPixel(uint8_t *buf, uint16_t x, uint16_t y, uint8_t black)
{
  if (buf == NULL)
  {
    return;
  }

  if (x >= EPD_3IN7_WIDTH_PX || y >= EPD_3IN7_HEIGHT_PX)
  {
    return;
  }

  uint32_t index = ((uint32_t)y * (uint32_t)EPD_3IN7_BYTES_PER_ROW) + ((uint32_t)x / 8u);
  uint8_t mask = (uint8_t)(0x80u >> (x & 7u));

  if (black)
  {
    buf[index] = (uint8_t)(buf[index] & (uint8_t)(~mask));
  }
  else
  {
    buf[index] = (uint8_t)(buf[index] | mask);
  }
}

static const uint8_t *EPD_Font5x7_Get(char c)
{
  static const uint8_t space[5] = {0x00, 0x00, 0x00, 0x00, 0x00};

  static const uint8_t a[5] = {0x20, 0x54, 0x54, 0x54, 0x78};
  static const uint8_t d[5] = {0x38, 0x44, 0x44, 0x48, 0x7F};
  static const uint8_t e[5] = {0x38, 0x54, 0x54, 0x54, 0x18};
  static const uint8_t h[5] = {0x7F, 0x08, 0x04, 0x04, 0x78};
  static const uint8_t l[5] = {0x00, 0x41, 0x7F, 0x40, 0x00};
  static const uint8_t o[5] = {0x38, 0x44, 0x44, 0x44, 0x38};
  static const uint8_t r[5] = {0x7C, 0x08, 0x04, 0x04, 0x08};
  static const uint8_t w[5] = {0x3C, 0x40, 0x30, 0x40, 0x3C};

  switch (c)
  {
  case 'a':
    return a;
  case 'd':
    return d;
  case 'e':
    return e;
  case 'h':
    return h;
  case 'l':
    return l;
  case 'o':
    return o;
  case 'r':
    return r;
  case 'w':
    return w;
  case ' ':
  default:
    return space;
  }
}

static void EPD_DrawChar5x7(uint8_t *buf, uint16_t x, uint16_t y, char c, uint8_t black, uint8_t scale)
{
  const uint8_t *glyph = EPD_Font5x7_Get(c);
  if (glyph == NULL || scale == 0u)
  {
    return;
  }

  for (uint8_t col = 0; col < 5u; col++)
  {
    uint8_t bits = glyph[col];
    for (uint8_t row = 0; row < 7u; row++)
    {
      uint8_t on = (uint8_t)((bits >> row) & 0x01u);
      if (on)
      {
        for (uint8_t dy = 0; dy < scale; dy++)
        {
          for (uint8_t dx = 0; dx < scale; dx++)
          {
            EPD_3IN7_SetPixel(buf, (uint16_t)(x + (uint16_t)col * scale + dx), (uint16_t)(y + (uint16_t)row * scale + dy), black);
          }
        }
      }
    }
  }
}

static void EPD_DrawString5x7(uint8_t *buf, uint16_t x, uint16_t y, const char *s, uint8_t black, uint8_t scale)
{
  if (buf == NULL || s == NULL || scale == 0u)
  {
    return;
  }

  const uint16_t adv = (uint16_t)(5u * scale + scale);
  uint16_t cursor_x = x;

  while (*s != '\0')
  {
    EPD_DrawChar5x7(buf, cursor_x, y, *s, black, scale);
    cursor_x = (uint16_t)(cursor_x + adv);
    s++;
  }
}

static void EPD_3IN7_MakeTestPattern(uint8_t invert)
{
  memset(epd_framebuffer, invert ? 0x00 : 0xFF, sizeof(epd_framebuffer));

  const uint16_t border = 8u;
  const uint8_t black = invert ? 0u : 1u;
  const uint8_t white = invert ? 1u : 0u;

  for (uint16_t y = 0; y < EPD_3IN7_HEIGHT_PX; y++)
  {
    for (uint16_t x = 0; x < EPD_3IN7_WIDTH_PX; x++)
    {
      if (x < border || x >= (EPD_3IN7_WIDTH_PX - border) || y < border || y >= (EPD_3IN7_HEIGHT_PX - border))
      {
        EPD_3IN7_SetPixel(epd_framebuffer, x, y, black);
      }
    }
  }

  const uint16_t box_w = 120u;
  const uint16_t box_h = 200u;
  const uint16_t x0 = (EPD_3IN7_WIDTH_PX - box_w) / 2u;
  const uint16_t y0 = (EPD_3IN7_HEIGHT_PX - box_h) / 2u;

  for (uint16_t y = y0; y < (uint16_t)(y0 + box_h); y++)
  {
    for (uint16_t x = x0; x < (uint16_t)(x0 + box_w); x++)
    {
      EPD_3IN7_SetPixel(epd_framebuffer, x, y, black);
    }
  }

  {
    const char *msg = "hello world";
    const uint8_t scale = 4u;
    const uint16_t adv = (uint16_t)(5u * scale + scale);
    const uint16_t msg_len = 11u;
    const uint16_t text_w = (uint16_t)(msg_len * adv - scale);
    const uint16_t text_h = (uint16_t)(7u * scale);

    uint16_t tx = x0;
    uint16_t ty = y0;
    if (box_w > text_w)
    {
      tx = (uint16_t)(x0 + (uint16_t)((box_w - text_w) / 2u));
    }
    if (box_h > text_h)
    {
      ty = (uint16_t)(y0 + (uint16_t)((box_h - text_h) / 2u));
    }

    EPD_DrawString5x7(epd_framebuffer, tx, ty, msg, white, scale);
  }

  for (uint16_t x = 0; x < EPD_3IN7_WIDTH_PX; x++)
  {
    uint32_t y1 = ((uint32_t)x * (uint32_t)EPD_3IN7_HEIGHT_PX) / (uint32_t)EPD_3IN7_WIDTH_PX;
    uint32_t y2 = (uint32_t)(EPD_3IN7_HEIGHT_PX - 1u) - y1;
    for (uint8_t t = 0; t < 3u; t++)
    {
      uint32_t yy1 = y1 + t;
      uint32_t yy2 = y2 + t;
      if (yy1 < EPD_3IN7_HEIGHT_PX)
      {
        EPD_3IN7_SetPixel(epd_framebuffer, x, (uint16_t)yy1, white);
      }
      if (yy2 < EPD_3IN7_HEIGHT_PX)
      {
        EPD_3IN7_SetPixel(epd_framebuffer, x, (uint16_t)yy2, white);
      }
    }
  }
}

static void Buzzer_Beep(uint32_t freq_hz, uint32_t duration_ms)
{
  if (freq_hz == 0u || duration_ms == 0u)
  {
    return;
  }

  uint32_t timer_clk_hz = HAL_RCC_GetPCLK2Freq();

  if ((RCC->CFGR & RCC_CFGR_PPRE2) != RCC_CFGR_PPRE2_DIV1)
  {
    timer_clk_hz *= 2u;
  }

  const uint32_t target_timer_hz = 1000000u;
  uint32_t prescaler = (timer_clk_hz / target_timer_hz);
  if (prescaler == 0u)
  {
    prescaler = 1u;
  }

  prescaler -= 1u;
  if (prescaler > 0xFFFFu)
  {
    prescaler = 0xFFFFu;
  }

  uint32_t pwm_hz = timer_clk_hz / (prescaler + 1u);
  uint32_t period = (pwm_hz / freq_hz);
  if (period < 2u)
  {
    period = 2u;
  }
  if (period > 0xFFFFu)
  {
    period = 0xFFFFu;
  }
  period -= 1u;

  __HAL_TIM_DISABLE(&htim15);
  __HAL_TIM_SET_PRESCALER(&htim15, prescaler);
  __HAL_TIM_SET_AUTORELOAD(&htim15, period);
  __HAL_TIM_SET_COMPARE(&htim15, TIM_CHANNEL_2, (period + 1u) / 2u);
  __HAL_TIM_SET_COUNTER(&htim15, 0u);
  HAL_TIM_GenerateEvent(&htim15, TIM_EVENTSOURCE_UPDATE);

  (void)HAL_TIM_PWM_Start(&htim15, TIM_CHANNEL_2);
  HAL_Delay(duration_ms);
  (void)HAL_TIM_PWM_Stop(&htim15, TIM_CHANNEL_2);
  __HAL_TIM_SET_COMPARE(&htim15, TIM_CHANNEL_2, 0u);
}

static uint8_t Button0_ActiveLow_PressedOnce(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState *last_state)
{
  GPIO_PinState btn = HAL_GPIO_ReadPin(port, pin);
  uint8_t pressed = 0u;

  if ((*last_state == GPIO_PIN_SET) && (btn == GPIO_PIN_RESET))
  {
    HAL_Delay(20);
    if (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET)
    {
      pressed = 1u;

      while (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET)
      {
        HAL_Delay(10);
      }

      btn = GPIO_PIN_SET;
    }
  }

  *last_state = btn;
  return pressed;
}

static void RgbLed_Init(void)
{
  uint32_t timer_clk_hz = HAL_RCC_GetPCLK1Freq();

  if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1)
  {
    timer_clk_hz *= 2u;
  }

  const uint32_t target_timer_hz = 1000000u;
  uint32_t prescaler = (timer_clk_hz / target_timer_hz);
  if (prescaler == 0u)
  {
    prescaler = 1u;
  }
  prescaler -= 1u;
  if (prescaler > 0xFFFFu)
  {
    prescaler = 0xFFFFu;
  }

  const uint32_t pwm_freq_hz = 1000u;
  uint32_t pwm_hz = timer_clk_hz / (prescaler + 1u);
  uint32_t period = (pwm_hz / pwm_freq_hz);
  if (period < 2u)
  {
    period = 2u;
  }
  if (period > 0xFFFFFFFFu)
  {
    period = 0xFFFFFFFFu;
  }
  period -= 1u;

  __HAL_TIM_DISABLE(&htim2);
  __HAL_TIM_SET_PRESCALER(&htim2, prescaler);
  __HAL_TIM_SET_AUTORELOAD(&htim2, period);
  __HAL_TIM_SET_COUNTER(&htim2, 0u);
  HAL_TIM_GenerateEvent(&htim2, TIM_EVENTSOURCE_UPDATE);

  (void)HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
  (void)HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
  (void)HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);

  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0u);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, 0u);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, 0u);
}

static void RgbLed_SetDuty5Pct(uint8_t r_on, uint8_t g_on, uint8_t b_on)
{
  uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim2);
  uint32_t on = (arr + 1u) / 20u;
  if (on == 0u)
  {
    on = 1u;
  }

  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, r_on ? on : 0u);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, g_on ? on : 0u);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, b_on ? on : 0u);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C2_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_SPI1_Init();
  MX_TIM15_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */

  (void)HAL_TIM_PWM_Stop(&htim15, TIM_CHANNEL_2);
  __HAL_TIM_SET_COMPARE(&htim15, TIM_CHANNEL_2, 0u);

  GPIO_PinState last_btn0 = HAL_GPIO_ReadPin(BTN_FUNCTION_0_GPIO_Port, BTN_FUNCTION_0_Pin);
  GPIO_PinState last_btn1 = HAL_GPIO_ReadPin(BTN_FUNCTION_1_GPIO_Port, BTN_FUNCTION_1_Pin);
  GPIO_PinState last_btn3 = HAL_GPIO_ReadPin(BTN_FUNCTION_3_GPIO_Port, BTN_FUNCTION_3_Pin);

  uint8_t epd_inited = 0u;
  uint8_t epd_invert = 0u;

  RS485_RxKick();

  RgbLed_Init();
  uint32_t rgb_next_ms = HAL_GetTick();
  uint8_t rgb_phase = 0u;
  const uint32_t rgb_step_ms = 333u; /* ~2 Hz full RGB cycle (3 phases) */

  uint32_t alive_next_ms = HAL_GetTick();
  uint32_t alive_count = 0u;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if ((int32_t)(HAL_GetTick() - rgb_next_ms) >= 0)
    {
      rgb_next_ms += rgb_step_ms;
      rgb_phase = (uint8_t)((rgb_phase + 1u) % 3u);

      if (rgb_phase == 0u)
      {
        RgbLed_SetDuty5Pct(1u, 0u, 0u);
      }
      else if (rgb_phase == 1u)
      {
        RgbLed_SetDuty5Pct(0u, 1u, 0u);
      }
      else
      {
        RgbLed_SetDuty5Pct(0u, 0u, 1u);
      }
    }

    if ((int32_t)(HAL_GetTick() - alive_next_ms) >= 0)
    {
      alive_next_ms += 1000u;
      printf("Alive %lu\r\n", (unsigned long)alive_count++);
    }

    if (Button0_ActiveLow_PressedOnce(BTN_FUNCTION_0_GPIO_Port, BTN_FUNCTION_0_Pin, &last_btn0))
    {
      Buzzer_Beep(2000u, 80u);
    }

    if (Button0_ActiveLow_PressedOnce(BTN_FUNCTION_1_GPIO_Port, BTN_FUNCTION_1_Pin, &last_btn1))
    {
      Buzzer_Beep(4000u, 500u);
    }

    if (rs485_rx_activity)
    {
      rs485_rx_activity = 0u;
      rs485_led_off_ms = HAL_GetTick() + 50u;
      HAL_GPIO_WritePin(SIG_LED_0_GPIO_Port, SIG_LED_0_Pin, GPIO_PIN_SET);
    }

    if (rs485_led_off_ms != 0u)
    {
      if ((int32_t)(HAL_GetTick() - rs485_led_off_ms) >= 0)
      {
        rs485_led_off_ms = 0u;
        HAL_GPIO_WritePin(SIG_LED_0_GPIO_Port, SIG_LED_0_Pin, GPIO_PIN_RESET);
      }
    }

    {
      uint8_t chunk[64];
      uint16_t n = RS485_RingPop(chunk, (uint16_t)sizeof(chunk));
      if (n > 0u)
      {
        (void)HAL_UART_Transmit(&huart1, chunk, n, 100u);
      }
    }

    if (Button0_ActiveLow_PressedOnce(BTN_FUNCTION_3_GPIO_Port, BTN_FUNCTION_3_Pin, &last_btn3))
    {
      if (!epd_inited)
      {
        epd_inited = (EPD_3IN7_Init_1Gray() == 0) ? 1u : 0u;
      }

      if (epd_inited)
      {
        epd_invert ^= 1u;
        EPD_3IN7_MakeTestPattern(epd_invert);
        (void)EPD_3IN7_Display1Gray(epd_framebuffer, (uint32_t)sizeof(epd_framebuffer), EPD_LUT_1GRAY_GC);
      }
      else
      {
        Buzzer_Beep(200u, 200u);
      }
    }

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 8;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x00B07CB4;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 4294967295;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief TIM15 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM15_Init(void)
{

  /* USER CODE BEGIN TIM15_Init 0 */

  /* USER CODE END TIM15_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM15_Init 1 */

  /* USER CODE END TIM15_Init 1 */
  htim15.Instance = TIM15;
  htim15.Init.Prescaler = 0;
  htim15.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim15.Init.Period = 65535;
  htim15.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim15.Init.RepetitionCounter = 0;
  htim15.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim15) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim15, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim15, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim15, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM15_Init 2 */

  /* USER CODE END TIM15_Init 2 */
  HAL_TIM_MspPostInit(&htim15);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_SWAP_INIT;
  huart1.AdvancedInit.Swap = UART_ADVFEATURE_SWAP_ENABLE;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_RS485Ex_Init(&huart2, UART_DE_POLARITY_HIGH, 0, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, DISP_CS_Pin|DISP_DC_Pin|DISP_RST_Pin|DISP_PWR_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SIG_LED_0_GPIO_Port, SIG_LED_0_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : UNUSED_Pin */
  GPIO_InitStruct.Pin = UNUSED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(UNUSED_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : DISP_CS_Pin DISP_DC_Pin DISP_RST_Pin DISP_PWR_Pin */
  GPIO_InitStruct.Pin = DISP_CS_Pin|DISP_DC_Pin|DISP_RST_Pin|DISP_PWR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : BTN_FUNCTION_0_Pin BTN_FUNCTION_1_Pin BTN_FUNCTION_2_Pin BTN_FUNCTION_3_Pin
                           BTN_FUNCTION_4_Pin BTN_FUNCTION_5_Pin */
  GPIO_InitStruct.Pin = BTN_FUNCTION_0_Pin|BTN_FUNCTION_1_Pin|BTN_FUNCTION_2_Pin|BTN_FUNCTION_3_Pin
                          |BTN_FUNCTION_4_Pin|BTN_FUNCTION_5_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : SIG_LED_0_Pin */
  GPIO_InitStruct.Pin = SIG_LED_0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SIG_LED_0_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : DISP_BUSY_Pin */
  GPIO_InitStruct.Pin = DISP_BUSY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DISP_BUSY_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : UNUSEDA15_Pin */
  GPIO_InitStruct.Pin = UNUSEDA15_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(UNUSEDA15_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : UNUSEDB7_Pin UNUSEDB8_Pin UNUSEDB9_Pin */
  GPIO_InitStruct.Pin = UNUSEDB7_Pin|UNUSEDB8_Pin|UNUSEDB9_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : BOOT0_Pin */
  GPIO_InitStruct.Pin = BOOT0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BOOT0_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
