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

#define BUTTON_EVENT_QUEUE_SIZE (16u)
#define BUTTON_DEBOUNCE_MS (25u)

static volatile uint8_t button_event_queue[BUTTON_EVENT_QUEUE_SIZE];
static volatile uint8_t button_event_head = 0u;
static volatile uint8_t button_event_tail = 0u;
static uint32_t button_last_edge_ms[6];
static uint8_t button_seen_mask = 0u;

void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
  uint8_t button;
  switch (gpio_pin)
  {
  case BTN_FUNCTION_0_Pin:
    button = 0u;
    break;
  case BTN_FUNCTION_1_Pin:
    button = 1u;
    break;
  case BTN_FUNCTION_2_Pin:
    button = 2u;
    break;
  case BTN_FUNCTION_3_Pin:
    button = 3u;
    break;
  case BTN_FUNCTION_4_Pin:
    button = 4u;
    break;
  case BTN_FUNCTION_5_Pin:
    button = 5u;
    break;
  default:
    return;
  }

  uint32_t now = HAL_GetTick();
  uint8_t button_mask = (uint8_t)(1u << button);
  if (((button_seen_mask & button_mask) != 0u) &&
      ((uint32_t)(now - button_last_edge_ms[button]) < BUTTON_DEBOUNCE_MS))
  {
    return;
  }

  button_seen_mask = (uint8_t)(button_seen_mask | button_mask);
  button_last_edge_ms[button] = now;

  uint8_t next = (uint8_t)((button_event_head + 1u) & (BUTTON_EVENT_QUEUE_SIZE - 1u));
  if (next != button_event_tail)
  {
    button_event_queue[button_event_head] = button;
    button_event_head = next;
  }
}

static uint8_t ButtonEvent_Pop(uint8_t *button)
{
  if (button == NULL || button_event_tail == button_event_head)
  {
    return 0u;
  }

  *button = button_event_queue[button_event_tail];
  button_event_tail = (uint8_t)((button_event_tail + 1u) & (BUTTON_EVENT_QUEUE_SIZE - 1u));
  return 1u;
}

#define EPD_3IN7_WIDTH_PX (280u)
#define EPD_3IN7_HEIGHT_PX (480u)
#define EPD_3IN7_BYTES_PER_ROW (EPD_3IN7_WIDTH_PX / 8u)
#define EPD_3IN7_BUF_SIZE ((EPD_3IN7_WIDTH_PX * EPD_3IN7_HEIGHT_PX) / 8u)
#define EPD_CANVAS_WIDTH_PX (480u)
#define EPD_CANVAS_HEIGHT_PX (280u)

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

static inline void EPD_CanvasSetPixel(uint8_t *buf, uint16_t x, uint16_t y, uint8_t black)
{
  if (x >= EPD_CANVAS_WIDTH_PX || y >= EPD_CANVAS_HEIGHT_PX)
  {
    return;
  }

  EPD_3IN7_SetPixel(buf, y, (uint16_t)(EPD_3IN7_HEIGHT_PX - x - 1u), black);
}

static const uint8_t *EPD_Font5x7_Get(char c)
{
  static const uint8_t space[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t digits[10][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}
  };
  static const uint8_t uppercase[26][5] = {
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C},
    {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01},
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}
  };
  static const uint8_t period[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
  static const uint8_t colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
  static const uint8_t dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};

  if (c >= 'a' && c <= 'z')
  {
    c = (char)(c - 'a' + 'A');
  }
  if (c >= '0' && c <= '9')
  {
    return digits[(uint8_t)(c - '0')];
  }
  if (c >= 'A' && c <= 'Z')
  {
    return uppercase[(uint8_t)(c - 'A')];
  }
  if (c == '.')
  {
    return period;
  }
  if (c == ':')
  {
    return colon;
  }
  if (c == '-')
  {
    return dash;
  }
  return space;
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
            EPD_CanvasSetPixel(buf, (uint16_t)(x + (uint16_t)col * scale + dx),
                               (uint16_t)(y + (uint16_t)row * scale + dy), black);
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

static uint16_t EPD_TextWidth5x7(const char *s, uint8_t scale)
{
  if (s == NULL || *s == '\0' || scale == 0u)
  {
    return 0u;
  }

  uint16_t length = 0u;
  while (s[length] != '\0')
  {
    length++;
  }
  return (uint16_t)(length * (uint16_t)(6u * scale) - scale);
}

static void EPD_DrawStringCentered5x7(uint8_t *buf, uint16_t y, const char *s, uint8_t black, uint8_t scale)
{
  uint16_t width = EPD_TextWidth5x7(s, scale);
  uint16_t x = (width < EPD_CANVAS_WIDTH_PX) ? (uint16_t)((EPD_CANVAS_WIDTH_PX - width) / 2u) : 0u;
  EPD_DrawString5x7(buf, x, y, s, black, scale);
}

static void EPD_FillRect(uint8_t *buf, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t black)
{
  uint16_t x_end = (uint16_t)(x + width);
  uint16_t y_end = (uint16_t)(y + height);
  if (x_end > EPD_CANVAS_WIDTH_PX)
  {
    x_end = EPD_CANVAS_WIDTH_PX;
  }
  if (y_end > EPD_CANVAS_HEIGHT_PX)
  {
    y_end = EPD_CANVAS_HEIGHT_PX;
  }

  for (uint16_t py = y; py < y_end; py++)
  {
    for (uint16_t px = x; px < x_end; px++)
    {
      EPD_CanvasSetPixel(buf, px, py, black);
    }
  }
}

static void EPD_DrawRect(uint8_t *buf, uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t black)
{
  if (width < 2u || height < 2u)
  {
    return;
  }
  EPD_FillRect(buf, x, y, width, 2u, black);
  EPD_FillRect(buf, x, (uint16_t)(y + height - 2u), width, 2u, black);
  EPD_FillRect(buf, x, y, 2u, height, black);
  EPD_FillRect(buf, (uint16_t)(x + width - 2u), y, 2u, height, black);
}

typedef struct
{
  uint16_t heading_deg;
  uint16_t course_deg;
  uint16_t speed_tenths;
  uint16_t distance_tenths;
  uint16_t trip_tenths;
} nav_demo_data_t;

typedef enum
{
  NAV_FIELD_HEADING = 0,
  NAV_FIELD_SPEED = 1,
  NAV_FIELD_COURSE = 2,
  NAV_FIELD_DISTANCE = 3
} nav_field_t;

#define NAV_VALUE_REGION_X (24u)
#define NAV_VALUE_REGION_Y (92u)
#define NAV_VALUE_REGION_WIDTH (270u)
#define NAV_VALUE_REGION_HEIGHT (80u)

static const char *Nav_FieldLabel(nav_field_t field)
{
  switch (field)
  {
  case NAV_FIELD_SPEED:
    return "SPEED";
  case NAV_FIELD_COURSE:
    return "COURSE";
  case NAV_FIELD_DISTANCE:
    return "DISTANCE";
  case NAV_FIELD_HEADING:
  default:
    return "HEADING";
  }
}

static const char *Nav_FieldUnit(nav_field_t field)
{
  switch (field)
  {
  case NAV_FIELD_SPEED:
    return "KT";
  case NAV_FIELD_COURSE:
    return "DEG";
  case NAV_FIELD_DISTANCE:
    return "NM";
  case NAV_FIELD_HEADING:
  default:
    return "DEG TRUE";
  }
}

static void Nav_FormatFieldValue(const nav_demo_data_t *nav, nav_field_t field, char *text, uint16_t text_size)
{
  if (nav == NULL || text == NULL || text_size == 0u)
  {
    return;
  }

  switch (field)
  {
  case NAV_FIELD_SPEED:
    (void)snprintf(text, text_size, "%u.%u", (unsigned int)(nav->speed_tenths / 10u),
                   (unsigned int)(nav->speed_tenths % 10u));
    break;
  case NAV_FIELD_COURSE:
    (void)snprintf(text, text_size, "%03u", (unsigned int)nav->course_deg);
    break;
  case NAV_FIELD_DISTANCE:
    (void)snprintf(text, text_size, "%u.%u", (unsigned int)(nav->distance_tenths / 10u),
                   (unsigned int)(nav->distance_tenths % 10u));
    break;
  case NAV_FIELD_HEADING:
  default:
    (void)snprintf(text, text_size, "%03u", (unsigned int)nav->heading_deg);
    break;
  }
}

static void Nav_AdjustField(nav_demo_data_t *nav, nav_field_t field, int8_t direction)
{
  if (nav == NULL || direction == 0)
  {
    return;
  }

  if (field == NAV_FIELD_HEADING)
  {
    nav->heading_deg = (direction > 0) ? (uint16_t)((nav->heading_deg + 1u) % 360u)
                                       : (uint16_t)((nav->heading_deg + 359u) % 360u);
  }
  else if (field == NAV_FIELD_COURSE)
  {
    nav->course_deg = (direction > 0) ? (uint16_t)((nav->course_deg + 1u) % 360u)
                                      : (uint16_t)((nav->course_deg + 359u) % 360u);
  }
  else if (field == NAV_FIELD_SPEED)
  {
    if (direction > 0)
    {
      nav->speed_tenths = (nav->speed_tenths < 999u) ? (uint16_t)(nav->speed_tenths + 1u) : 0u;
    }
    else
    {
      nav->speed_tenths = (nav->speed_tenths > 0u) ? (uint16_t)(nav->speed_tenths - 1u) : 999u;
    }
  }
  else
  {
    if (direction > 0)
    {
      nav->distance_tenths = (nav->distance_tenths < 999u) ? (uint16_t)(nav->distance_tenths + 1u) : 0u;
    }
    else
    {
      nav->distance_tenths = (nav->distance_tenths > 0u) ? (uint16_t)(nav->distance_tenths - 1u) : 999u;
    }
  }
}

static void EPD_DrawStringCenteredInBox(uint16_t x, uint16_t width, uint16_t y, const char *text,
                                        uint8_t black, uint8_t scale)
{
  uint16_t text_width = EPD_TextWidth5x7(text, scale);
  uint16_t text_x = (text_width < width) ? (uint16_t)(x + (width - text_width) / 2u) : x;
  EPD_DrawString5x7(epd_framebuffer, text_x, y, text, black, scale);
}

static void EPD_DrawSelectedNavValue(const nav_demo_data_t *nav, nav_field_t selected)
{
  char value[8];
  Nav_FormatFieldValue(nav, selected, value, (uint16_t)sizeof(value));

  EPD_FillRect(epd_framebuffer, NAV_VALUE_REGION_X, NAV_VALUE_REGION_Y,
               NAV_VALUE_REGION_WIDTH, NAV_VALUE_REGION_HEIGHT, 0u);
  EPD_DrawStringCenteredInBox(10u, 290u, 101u, value, 1u, 9u);
}

static void EPD_DrawNavSummaryCard(uint16_t y, const nav_demo_data_t *nav, nav_field_t field,
                                   nav_field_t selected)
{
  char value[8];
  const uint8_t is_selected = (field == selected) ? 1u : 0u;
  const uint8_t ink = is_selected ? 0u : 1u;
  if (is_selected)
  {
    (void)snprintf(value, sizeof(value), "ACTIVE");
  }
  else
  {
    Nav_FormatFieldValue(nav, field, value, (uint16_t)sizeof(value));
  }

  if (is_selected)
  {
    EPD_FillRect(epd_framebuffer, 312u, y, 158u, 40u, 1u);
  }
  else
  {
    EPD_DrawRect(epd_framebuffer, 312u, y, 158u, 40u, 1u);
  }

  EPD_DrawString5x7(epd_framebuffer, 320u, (uint16_t)(y + 6u), Nav_FieldLabel(field), ink, 1u);
  EPD_DrawStringCenteredInBox(312u, 158u, (uint16_t)(y + 20u), value, ink, 2u);
}

static void EPD_3IN7_MakeNavigationDisplay(const nav_demo_data_t *nav, nav_field_t selected)
{
  char trip[8];
  char title[24];

  if (nav == NULL)
  {
    return;
  }

  (void)snprintf(trip, sizeof(trip), "%u.%u", (unsigned int)(nav->trip_tenths / 10u),
                 (unsigned int)(nav->trip_tenths % 10u));
  (void)snprintf(title, sizeof(title), "NAV - %s", Nav_FieldLabel(selected));

  memset(epd_framebuffer, 0xFF, sizeof(epd_framebuffer));

  EPD_FillRect(epd_framebuffer, 0u, 0u, EPD_CANVAS_WIDTH_PX, 42u, 1u);
  EPD_DrawStringCentered5x7(epd_framebuffer, 10u, title, 0u, 3u);

  EPD_DrawRect(epd_framebuffer, 10u, 52u, 290u, 178u, 1u);
  EPD_DrawStringCenteredInBox(10u, 290u, 65u, Nav_FieldLabel(selected), 1u, 3u);
  EPD_DrawSelectedNavValue(nav, selected);
  EPD_DrawStringCenteredInBox(10u, 290u, 184u, Nav_FieldUnit(selected), 1u, 3u);

  EPD_DrawNavSummaryCard(52u, nav, NAV_FIELD_HEADING, selected);
  EPD_DrawNavSummaryCard(97u, nav, NAV_FIELD_SPEED, selected);
  EPD_DrawNavSummaryCard(142u, nav, NAV_FIELD_COURSE, selected);
  EPD_DrawNavSummaryCard(187u, nav, NAV_FIELD_DISTANCE, selected);

  EPD_FillRect(epd_framebuffer, 0u, 240u, EPD_CANVAS_WIDTH_PX, 40u, 1u);
  EPD_DrawString5x7(epd_framebuffer, 10u, 247u, "B0 UP  B1 DOWN", 0u, 1u);
  EPD_DrawString5x7(epd_framebuffer, 10u, 264u, "B2 HDG  B3 SPD  B4 CRS  B5 DST", 0u, 1u);
  EPD_DrawString5x7(epd_framebuffer, 374u, 247u, "TRIP", 0u, 1u);
  EPD_DrawString5x7(epd_framebuffer, 410u, 247u, trip, 0u, 1u);
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

static void RgbLed_StartupSequence(void)
{
  for (uint8_t flash = 0u; flash < 3u; flash++)
  {
    RgbLed_SetDuty5Pct(0u, 1u, 0u);
    HAL_Delay(75u);
    RgbLed_SetDuty5Pct(0u, 0u, 0u);
    HAL_Delay(75u);
  }

  RgbLed_SetDuty5Pct(0u, 1u, 0u);
}

static void Button_ClickFeedback(void)
{
  HAL_GPIO_WritePin(SIG_LED_0_GPIO_Port, SIG_LED_0_Pin, GPIO_PIN_SET);
  Buzzer_Beep(2500u, 40u);
  HAL_GPIO_WritePin(SIG_LED_0_GPIO_Port, SIG_LED_0_Pin, GPIO_PIN_RESET);
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

  nav_demo_data_t nav_data = {
    .heading_deg = 123u,
    .course_deg = 128u,
    .speed_tenths = 64u,
    .distance_tenths = 32u,
    .trip_tenths = 187u
  };
  nav_field_t selected_field = NAV_FIELD_HEADING;
  uint8_t epd_inited = 0u;

  RS485_RxKick();

  RgbLed_Init();
  RgbLed_StartupSequence();

  uint32_t alive_next_ms = HAL_GetTick();
  uint32_t alive_count = 0u;

  epd_inited = (EPD_3IN7_Init_1Gray() == 0) ? 1u : 0u;
  if (epd_inited)
  {
    EPD_3IN7_MakeNavigationDisplay(&nav_data, selected_field);
    if (EPD_3IN7_Display1Gray(epd_framebuffer, (uint32_t)sizeof(epd_framebuffer), EPD_LUT_1GRAY_GC) != 0)
    {
      epd_inited = 0u;
    }
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if ((int32_t)(HAL_GetTick() - alive_next_ms) >= 0)
    {
      alive_next_ms += 1000u;
      printf("Alive %lu\r\n", (unsigned long)alive_count++);
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

    uint8_t button;
    if (ButtonEvent_Pop(&button))
    {
      uint8_t needs_full_refresh = 0u;
      do
      {
        Button_ClickFeedback();

        if (button == 0u)
        {
          Nav_AdjustField(&nav_data, selected_field, 1);
        }
        else if (button == 1u)
        {
          Nav_AdjustField(&nav_data, selected_field, -1);
        }
        else
        {
          selected_field = (nav_field_t)(button - 2u);
          needs_full_refresh = 1u;
        }
      } while (ButtonEvent_Pop(&button));

      uint8_t display_was_ready = epd_inited;
      if (!epd_inited)
      {
        epd_inited = (EPD_3IN7_Init_1Gray() == 0) ? 1u : 0u;
      }
      else if (needs_full_refresh)
      {
        /* Return to the panel's full-refresh setup after using the A2 waveform. */
        epd_inited = (EPD_3IN7_Init_1Gray() == 0) ? 1u : 0u;
      }

      if (epd_inited)
      {
        int display_result;
        if (!needs_full_refresh && display_was_ready)
        {
          EPD_DrawSelectedNavValue(&nav_data, selected_field);
          /* The 3.7-inch controller needs a complete RAM image even for A2.
           * The A2 waveform changes only pixels that differ on the panel. */
          display_result = EPD_3IN7_Display1Gray(
            epd_framebuffer, (uint32_t)sizeof(epd_framebuffer), EPD_LUT_1GRAY_A2);
        }
        else
        {
          EPD_3IN7_MakeNavigationDisplay(&nav_data, selected_field);
          display_result = EPD_3IN7_Display1Gray(
            epd_framebuffer, (uint32_t)sizeof(epd_framebuffer), EPD_LUT_1GRAY_GC);
        }

        if (display_result != 0)
        {
          epd_inited = 0u;
        }
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

  GPIO_InitStruct.Pin = BTN_FUNCTION_0_Pin|BTN_FUNCTION_1_Pin|BTN_FUNCTION_2_Pin|BTN_FUNCTION_3_Pin
                          |BTN_FUNCTION_4_Pin|BTN_FUNCTION_5_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  __HAL_GPIO_EXTI_CLEAR_IT(BTN_FUNCTION_0_Pin|BTN_FUNCTION_1_Pin|BTN_FUNCTION_2_Pin|BTN_FUNCTION_3_Pin
                            |BTN_FUNCTION_4_Pin|BTN_FUNCTION_5_Pin);

  HAL_NVIC_SetPriority(EXTI0_IRQn, 6u, 0u);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);
  HAL_NVIC_SetPriority(EXTI1_IRQn, 6u, 0u);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);
  HAL_NVIC_SetPriority(EXTI2_IRQn, 6u, 0u);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);
  HAL_NVIC_SetPriority(EXTI4_IRQn, 6u, 0u);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 6u, 0u);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

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
