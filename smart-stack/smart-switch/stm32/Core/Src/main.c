/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#include "app.h"
#include <stdio.h>
// Use shared SWO trace function implemented in App/lib.cpp
#include "lib.hpp"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Debug instrumentation switch: set to 1 temporarily to enable SDADC bring-up helpers */
#ifndef SDADC_DEBUG
#define SDADC_DEBUG 1
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SMBUS_HandleTypeDef hsmbus1;

SDADC_HandleTypeDef hsdadc1;

TIM_HandleTypeDef htim4;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_SMBUS_Init(void);
static void MX_TIM4_Init(void);
static void MX_SDADC1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
/* Debug helpers (prototypes) */
/* Always-on: ensure internal VREF is enabled and ready before SDADC init */
static void dump_pwr_sdadc(void);
static void dump_rcc_summary(void);
static void swo_trace_kv_hex32(const char *key, uint32_t value);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_I2C1_SMBUS_Init();
  MX_TIM4_Init();
  MX_SDADC1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  app_init();
  
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  swo_trace_line_level(LOG_INFO, "Alrighty... lets go");
  while (1)
  {
    app_step();

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2|RCC_PERIPHCLK_I2C1
                              |RCC_PERIPHCLK_SDADC;
  PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
  PeriphClkInit.SdadcClockSelection = RCC_SDADCSYSCLK_DIV4;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_PWREx_EnableSDADC(PWR_SDADC_ANALOG1);
  HAL_RCC_MCOConfig(RCC_MCO, RCC_MCO1SOURCE_SYSCLK, RCC_MCODIV_1);
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_SMBUS_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hsmbus1.Instance = I2C1;
  hsmbus1.Init.Timing = 0x00201D2B;
  hsmbus1.Init.AnalogFilter = SMBUS_ANALOGFILTER_ENABLE;
  hsmbus1.Init.OwnAddress1 = 2;
  hsmbus1.Init.AddressingMode = SMBUS_ADDRESSINGMODE_7BIT;
  hsmbus1.Init.DualAddressMode = SMBUS_DUALADDRESS_DISABLE;
  hsmbus1.Init.OwnAddress2 = 0;
  hsmbus1.Init.OwnAddress2Masks = SMBUS_OA2_NOMASK;
  hsmbus1.Init.GeneralCallMode = SMBUS_GENERALCALL_DISABLE;
  hsmbus1.Init.NoStretchMode = SMBUS_NOSTRETCH_DISABLE;
  hsmbus1.Init.PacketErrorCheckMode = SMBUS_PEC_DISABLE;
  hsmbus1.Init.PeripheralMode = SMBUS_PERIPHERAL_MODE_SMBUS_SLAVE;
  hsmbus1.Init.SMBusTimeout = 0x00008061;
  if (HAL_SMBUS_Init(&hsmbus1) != HAL_OK)
  {
    Error_Handler();
  }

  /** configuration Alert Mode
  */
  if (HAL_SMBUS_EnableAlert_IT(&hsmbus1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SDADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SDADC1_Init(void)
{

  /* USER CODE BEGIN SDADC1_Init 0 */

  /* USER CODE END SDADC1_Init 0 */

  SDADC_ConfParamTypeDef ConfParamStruct = {0};

  /* USER CODE BEGIN SDADC1_Init 1 */

  /* USER CODE END SDADC1_Init 1 */

  /** Configure the SDADC low power mode, fast conversion mode,
  slow clock mode and SDADC1 reference voltage
  */
  hsdadc1.Instance = SDADC1;
  hsdadc1.Init.IdleLowPowerMode = SDADC_LOWPOWER_NONE;
  hsdadc1.Init.FastConversionMode = SDADC_FAST_CONV_DISABLE;
  hsdadc1.Init.SlowClockMode = SDADC_SLOW_CLOCK_DISABLE;
  hsdadc1.Init.ReferenceVoltage = SDADC_VREF_EXT;
  if (HAL_SDADC_Init(&hsdadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Set parameters for SDADC configuration 0 Register
  */
  ConfParamStruct.InputMode = SDADC_INPUT_MODE_SE_ZERO_REFERENCE;
  ConfParamStruct.Gain = SDADC_GAIN_1;
  ConfParamStruct.CommonMode = SDADC_COMMON_MODE_VSSA;
  ConfParamStruct.Offset = 0;
  if (HAL_SDADC_PrepareChannelConfig(&hsdadc1, SDADC_CONF_INDEX_0, &ConfParamStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SDADC1_Init 2 */

  /* USER CODE END SDADC1_Init 2 */

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 3;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 1023;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

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
  if (HAL_UART_Init(&huart2) != HAL_OK)
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
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, RELAY_ON_Pin|RELAY_OFF_Pin|MCU_FEEDBACK_Pin|CUR_SENSE_ON_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TEMP_SENSE_ON_GPIO_Port, TEMP_SENSE_ON_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, FEEDBACK_LED_Pin|MCU_ERROR_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : UNUSED_Pin UNUSEDC14_Pin UNUSEDC15_Pin */
  GPIO_InitStruct.Pin = UNUSED_Pin|UNUSEDC14_Pin|UNUSEDC15_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : RELAY_ON_Pin RELAY_OFF_Pin TEMP_SENSE_ON_Pin MCU_FEEDBACK_Pin
                           CUR_SENSE_ON_Pin */
  GPIO_InitStruct.Pin = RELAY_ON_Pin|RELAY_OFF_Pin|TEMP_SENSE_ON_Pin|MCU_FEEDBACK_Pin
                          |CUR_SENSE_ON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : MCU_DIG_DEV_VS_IN_Pin MCU_DIG_DEV_VS_OUT_Pin */
  GPIO_InitStruct.Pin = MCU_DIG_DEV_VS_IN_Pin|MCU_DIG_DEV_VS_OUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : FEEDBACK_LED_Pin MCU_ERROR_Pin */
  GPIO_InitStruct.Pin = FEEDBACK_LED_Pin|MCU_ERROR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : UNUSEDE8_Pin */
  GPIO_InitStruct.Pin = UNUSEDE8_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(UNUSEDE8_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : UNUSEDB14_Pin UNUSEDB15_Pin */
  GPIO_InitStruct.Pin = UNUSEDB14_Pin|UNUSEDB15_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : UNUSEDD8_Pin */
  GPIO_InitStruct.Pin = UNUSEDD8_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(UNUSEDD8_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PA8 */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF0_MCO;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : MCU_TOGGLE_Pin BTN_FUNCTION_Pin */
  GPIO_InitStruct.Pin = MCU_TOGGLE_Pin|BTN_FUNCTION_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : UNUSEDF7_Pin */
  GPIO_InitStruct.Pin = UNUSEDF7_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(UNUSEDF7_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : BTN_TOGGLE_Pin */
  GPIO_InitStruct.Pin = BTN_TOGGLE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BTN_TOGGLE_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

// *************************************************************************************************
 
static void safe_delay_ms(uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();
    for (volatile uint32_t i = 0; i < 5000U; ++i)
    {
        __NOP();
    }
    uint32_t t1 = HAL_GetTick();
    if (t1 != t0)
    {
        HAL_Delay(ms);
        return;
    }
    volatile uint32_t cycles = ms * 10000U;
    while (cycles--)
    {
        __NOP();
    }
}

static void safe_delay_us(uint32_t us)
{
    /* Prefer DWT cycle counter for microsecond delays if available */
    /* Ensure tracing is enabled */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t cycles_per_us = SystemCoreClock / 1000000U;
    if (cycles_per_us == 0U)
    {
        cycles_per_us = 1U;
    }
    uint32_t start = DWT->CYCCNT;
    uint32_t wait_cycles = us * cycles_per_us;
    /* If DWT counter is incrementing, use it */
    uint32_t check = DWT->CYCCNT;
    if (check != start)
    {
        while ((uint32_t)(DWT->CYCCNT - start) < wait_cycles)
        {
            __NOP();
        }
        return;
    }
    /* Fallback crude busy-wait if DWT not ticking */
    volatile uint32_t loops = (cycles_per_us / 8U + 1U) * us;
    while (loops--)
    {
        __NOP();
    }
}
 
// *************************************************************************************************

static void swo_trace_kv_hex32(const char *key, uint32_t value)
{
  char buf[64];
  int i = 0;
  if (key)
  {
    while (key[i] != '\0' && i < (int)sizeof(buf) - 1)
    {
      buf[i] = key[i];
      i++;
    }
    if (i < (int)sizeof(buf) - 1)
    {
      buf[i++] = '=';
    }
  }
  if (i < (int)sizeof(buf) - 1) buf[i++] = '0';
  if (i < (int)sizeof(buf) - 1) buf[i++] = 'x';
  for (int nib = 7; nib >= 0 && i < (int)sizeof(buf) - 1; --nib)
  {
    uint8_t d = (uint8_t)((value >> (nib * 4)) & 0xFU);
    buf[i++] = (char)(d < 10 ? ('0' + d) : ('A' + (d - 10)));
  }
  buf[i] = '\0';
  swo_trace_line_level(LOG_VERBOSE, buf);
}

/* Always-available dumps used by Error_Handler and debug pre-init */
static void dump_pwr_sdadc(void)
{
  swo_trace_kv_hex32("PWR CR", PWR->CR);
  swo_trace_kv_hex32("PWR CSR", PWR->CSR);
}

static void dump_rcc_summary(void)
{
  swo_trace_kv_hex32("RCC CFGR", RCC->CFGR);
  swo_trace_kv_hex32("RCC CFGR2", RCC->CFGR2);
  swo_trace_kv_hex32("RCC CFGR3", RCC->CFGR3);
  swo_trace_kv_hex32("RCC APB1ENR", RCC->APB1ENR);
  swo_trace_kv_hex32("RCC APB2ENR", RCC->APB2ENR);
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */

    swo_trace_line_level(LOG_FATAL, "ERROR: Error_Handler()");
    {
        swo_trace_kv_hex32("SDADC1 state", (uint32_t)HAL_SDADC_GetState(&hsdadc1));
        swo_trace_kv_hex32("SDADC1 err", (uint32_t)HAL_SDADC_GetError(&hsdadc1));
#if SDADC_DEBUG
        /* Detailed SDADC registers (debug only) */
        swo_trace_kv_hex32("SDADC1 CR1", SDADC1->CR1);
        swo_trace_kv_hex32("SDADC1 CR2", SDADC1->CR2);
        swo_trace_kv_hex32("SDADC1 ISR", SDADC1->ISR);
#endif
        dump_pwr_sdadc();
        dump_rcc_summary();
    }

    /* Ensure PB8 (IND_LED_GRN) is configured as output in case GPIO init didn't run */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gi = {0};
    gi.Pin = IND_LED_GRN_Pin;
    gi.Mode = GPIO_MODE_OUTPUT_PP;
    gi.Pull = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(IND_LED_GRN_GPIO_Port, &gi);
    HAL_GPIO_WritePin(IND_LED_GRN_GPIO_Port, IND_LED_GRN_Pin, GPIO_PIN_RESET);

    const uint32_t on_us = 100U;
    const uint32_t on_ms = 0U; /* accounted in us for the ON pulse */
    const uint32_t off_ms = 100U;
    const uint32_t cycle_ms = 1000U;
    const uint32_t burst_ms = 3U * (on_ms + off_ms);
    const uint32_t rest_ms = (cycle_ms > burst_ms) ? (cycle_ms - burst_ms) : 0U;
    while (1)
    {
        for (int i = 0; i < 3; ++i)
        {
            HAL_GPIO_WritePin(IND_LED_GRN_GPIO_Port, IND_LED_GRN_Pin, GPIO_PIN_SET);
            safe_delay_us(on_us);
            HAL_GPIO_WritePin(IND_LED_GRN_GPIO_Port, IND_LED_GRN_Pin, GPIO_PIN_RESET);
            safe_delay_ms(off_ms);
        }
        safe_delay_ms(rest_ms);
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
