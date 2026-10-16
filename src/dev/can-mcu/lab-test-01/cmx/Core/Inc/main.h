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
#define GPIO_PC13_F0_Pin GPIO_PIN_13
#define GPIO_PC13_F0_GPIO_Port GPIOC
#define ADC1_IN5_Pin GPIO_PIN_0
#define ADC1_IN5_GPIO_Port GPIOA
#define ADC1_IN6_Pin GPIO_PIN_1
#define ADC1_IN6_GPIO_Port GPIOA
#define ADC1_IN7_Pin GPIO_PIN_2
#define ADC1_IN7_GPIO_Port GPIOA
#define ADC1_IN8_Pin GPIO_PIN_3
#define ADC1_IN8_GPIO_Port GPIOA
#define GPIO_PA4_Pin GPIO_PIN_4
#define GPIO_PA4_GPIO_Port GPIOA
#define GPIO_PA5_Pin GPIO_PIN_5
#define GPIO_PA5_GPIO_Port GPIOA
#define GPIO_PA6_Pin GPIO_PIN_6
#define GPIO_PA6_GPIO_Port GPIOA
#define GPIO_PA7_Pin GPIO_PIN_7
#define GPIO_PA7_GPIO_Port GPIOA
#define CAN_ACTIVE_Pin GPIO_PIN_0
#define CAN_ACTIVE_GPIO_Port GPIOB
#define CAN_MUTE_Pin GPIO_PIN_1
#define CAN_MUTE_GPIO_Port GPIOB
#define GPIO_PB2_F2_Pin GPIO_PIN_2
#define GPIO_PB2_F2_GPIO_Port GPIOB
#define GPIO_PB10_F4_Pin GPIO_PIN_10
#define GPIO_PB10_F4_GPIO_Port GPIOB
#define GPIO_PB11_F5_Pin GPIO_PIN_11
#define GPIO_PB11_F5_GPIO_Port GPIOB
#define HOST_POWER_Pin GPIO_PIN_15
#define HOST_POWER_GPIO_Port GPIOB
#define GPIO_PA8_T11_Pin GPIO_PIN_8
#define GPIO_PA8_T11_GPIO_Port GPIOA
#define GPIO_PA9_T12_Pin GPIO_PIN_9
#define GPIO_PA9_T12_GPIO_Port GPIOA
#define GPIO_PA10_Pin GPIO_PIN_10
#define GPIO_PA10_GPIO_Port GPIOA
#define CAN_RX_Pin GPIO_PIN_11
#define CAN_RX_GPIO_Port GPIOA
#define CAN_TX_Pin GPIO_PIN_12
#define CAN_TX_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define GPIO_PA15_F1_Pin GPIO_PIN_15
#define GPIO_PA15_F1_GPIO_Port GPIOA
#define TRACESWO_Pin GPIO_PIN_3
#define TRACESWO_GPIO_Port GPIOB
#define GPIO_PB4_F3_Pin GPIO_PIN_4
#define GPIO_PB4_F3_GPIO_Port GPIOB
#define GPIO_PB5_FP_Pin GPIO_PIN_5
#define GPIO_PB5_FP_GPIO_Port GPIOB
#define STL_COM_TX_Pin GPIO_PIN_6
#define STL_COM_TX_GPIO_Port GPIOB
#define STL_COM_RX_Pin GPIO_PIN_7
#define STL_COM_RX_GPIO_Port GPIOB
#define BOOT0_Pin GPIO_PIN_3
#define BOOT0_GPIO_Port GPIOH
#define I2C_SCL_Pin GPIO_PIN_8
#define I2C_SCL_GPIO_Port GPIOB
#define I2C_SDA_Pin GPIO_PIN_9
#define I2C_SDA_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
