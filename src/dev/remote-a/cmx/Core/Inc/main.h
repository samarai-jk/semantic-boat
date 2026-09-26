/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define UNUSED_Pin GPIO_PIN_13
#define UNUSED_GPIO_Port GPIOC
#define STATUS_LED_RED_Pin GPIO_PIN_0
#define STATUS_LED_RED_GPIO_Port GPIOA
#define MCU_485_DE_Pin GPIO_PIN_1
#define MCU_485_DE_GPIO_Port GPIOA
#define MCU_485_TX_Pin GPIO_PIN_2
#define MCU_485_TX_GPIO_Port GPIOA
#define MCU_485_RX_Pin GPIO_PIN_3
#define MCU_485_RX_GPIO_Port GPIOA
#define DISP_CS_Pin GPIO_PIN_4
#define DISP_CS_GPIO_Port GPIOA
#define DISP_SCK_Pin GPIO_PIN_5
#define DISP_SCK_GPIO_Port GPIOA
#define DISP_DC_Pin GPIO_PIN_6
#define DISP_DC_GPIO_Port GPIOA
#define DISP_MOSI_Pin GPIO_PIN_7
#define DISP_MOSI_GPIO_Port GPIOA
#define BTN_FUNCTION_0_Pin GPIO_PIN_0
#define BTN_FUNCTION_0_GPIO_Port GPIOB
#define BTN_FUNCTION_1_Pin GPIO_PIN_1
#define BTN_FUNCTION_1_GPIO_Port GPIOB
#define BTN_FUNCTION_2_Pin GPIO_PIN_2
#define BTN_FUNCTION_2_GPIO_Port GPIOB
#define STATUS_LED_GRN_Pin GPIO_PIN_10
#define STATUS_LED_GRN_GPIO_Port GPIOB
#define STATUS_LED_BLU_Pin GPIO_PIN_11
#define STATUS_LED_BLU_GPIO_Port GPIOB
#define SIG_LED_0_Pin GPIO_PIN_12
#define SIG_LED_0_GPIO_Port GPIOB
#define MCU_I2C_SCL_Pin GPIO_PIN_13
#define MCU_I2C_SCL_GPIO_Port GPIOB
#define MCU_I2C_SDA_Pin GPIO_PIN_14
#define MCU_I2C_SDA_GPIO_Port GPIOB
#define MCU_BUZ_Pin GPIO_PIN_15
#define MCU_BUZ_GPIO_Port GPIOB
#define DISP_RST_Pin GPIO_PIN_8
#define DISP_RST_GPIO_Port GPIOA
#define STL_COM_RX_Pin GPIO_PIN_9
#define STL_COM_RX_GPIO_Port GPIOA
#define STL_COM_TX_Pin GPIO_PIN_10
#define STL_COM_TX_GPIO_Port GPIOA
#define DISP_BUSY_Pin GPIO_PIN_11
#define DISP_BUSY_GPIO_Port GPIOA
#define DISP_PWR_Pin GPIO_PIN_12
#define DISP_PWR_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define UNUSEDA15_Pin GPIO_PIN_15
#define UNUSEDA15_GPIO_Port GPIOA
#define TRACESWO_Pin GPIO_PIN_3
#define TRACESWO_GPIO_Port GPIOB
#define BTN_FUNCTION_3_Pin GPIO_PIN_4
#define BTN_FUNCTION_3_GPIO_Port GPIOB
#define BTN_FUNCTION_4_Pin GPIO_PIN_5
#define BTN_FUNCTION_4_GPIO_Port GPIOB
#define BTN_FUNCTION_5_Pin GPIO_PIN_6
#define BTN_FUNCTION_5_GPIO_Port GPIOB
#define UNUSEDB7_Pin GPIO_PIN_7
#define UNUSEDB7_GPIO_Port GPIOB
#define BOOT0_Pin GPIO_PIN_3
#define BOOT0_GPIO_Port GPIOH
#define UNUSEDB8_Pin GPIO_PIN_8
#define UNUSEDB8_GPIO_Port GPIOB
#define UNUSEDB9_Pin GPIO_PIN_9
#define UNUSEDB9_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
