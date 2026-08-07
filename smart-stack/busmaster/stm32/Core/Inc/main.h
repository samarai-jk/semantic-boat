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
#define UNUSEDA4_Pin GPIO_PIN_4
#define UNUSEDA4_GPIO_Port GPIOA
#define UNUSEDA5_Pin GPIO_PIN_5
#define UNUSEDA5_GPIO_Port GPIOA
#define UNUSEDA6_Pin GPIO_PIN_6
#define UNUSEDA6_GPIO_Port GPIOA
#define UNUSEDA7_Pin GPIO_PIN_7
#define UNUSEDA7_GPIO_Port GPIOA
#define CAN_ACTIVE_Pin GPIO_PIN_0
#define CAN_ACTIVE_GPIO_Port GPIOB
#define CAN_MUTE_Pin GPIO_PIN_1
#define CAN_MUTE_GPIO_Port GPIOB
#define UNUSEDB2_Pin GPIO_PIN_2
#define UNUSEDB2_GPIO_Port GPIOB
#define STATUS_LED_GRN_Pin GPIO_PIN_10
#define STATUS_LED_GRN_GPIO_Port GPIOB
#define STATUS_LED_BLU_Pin GPIO_PIN_11
#define STATUS_LED_BLU_GPIO_Port GPIOB
#define UNUSEDB12_Pin GPIO_PIN_12
#define UNUSEDB12_GPIO_Port GPIOB
#define MCU_I2C_SCL_Pin GPIO_PIN_13
#define MCU_I2C_SCL_GPIO_Port GPIOB
#define MCU_I2C_SDA_Pin GPIO_PIN_14
#define MCU_I2C_SDA_GPIO_Port GPIOB
#define UNUSEDB15_Pin GPIO_PIN_15
#define UNUSEDB15_GPIO_Port GPIOB
#define UNUSEDA8_Pin GPIO_PIN_8
#define UNUSEDA8_GPIO_Port GPIOA
#define STL_COM_RX_Pin GPIO_PIN_9
#define STL_COM_RX_GPIO_Port GPIOA
#define STL_COM_TX_Pin GPIO_PIN_10
#define STL_COM_TX_GPIO_Port GPIOA
#define CAN_RX_Pin GPIO_PIN_11
#define CAN_RX_GPIO_Port GPIOA
#define CAN_TX_Pin GPIO_PIN_12
#define CAN_TX_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define UNUSEDA15_Pin GPIO_PIN_15
#define UNUSEDA15_GPIO_Port GPIOA
#define TRACESWO_Pin GPIO_PIN_3
#define TRACESWO_GPIO_Port GPIOB
#define UNUSEDB4_Pin GPIO_PIN_4
#define UNUSEDB4_GPIO_Port GPIOB
#define SMU_SMB_ALERT_Pin GPIO_PIN_5
#define SMU_SMB_ALERT_GPIO_Port GPIOB
#define MCU_SMB_CLK_Pin GPIO_PIN_6
#define MCU_SMB_CLK_GPIO_Port GPIOB
#define MCU_SMB_DATA_Pin GPIO_PIN_7
#define MCU_SMB_DATA_GPIO_Port GPIOB
#define BOOT0_Pin GPIO_PIN_3
#define BOOT0_GPIO_Port GPIOH
#define MCU_STACK_ENABLE_Pin GPIO_PIN_8
#define MCU_STACK_ENABLE_GPIO_Port GPIOB
#define BTN_FUNCTION_0_Pin GPIO_PIN_9
#define BTN_FUNCTION_0_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
