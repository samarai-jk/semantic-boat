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
#define DI23_RESERVE1_Pin GPIO_PIN_13
#define DI23_RESERVE1_GPIO_Port GPIOC
#define DI10_RPM_Pin GPIO_PIN_0
#define DI10_RPM_GPIO_Port GPIOA
#define DI11_IGNITION_Pin GPIO_PIN_1
#define DI11_IGNITION_GPIO_Port GPIOA
#define LED_RED_Pin GPIO_PIN_2
#define LED_RED_GPIO_Port GPIOA
#define DI12_STARTER_SOLENOID_Pin GPIO_PIN_3
#define DI12_STARTER_SOLENOID_GPIO_Port GPIOA
#define AI0_WET_EXHAUST_Pin GPIO_PIN_4
#define AI0_WET_EXHAUST_GPIO_Port GPIOA
#define AI1_ALTERNATOR_Pin GPIO_PIN_5
#define AI1_ALTERNATOR_GPIO_Port GPIOA
#define AI2_HEAT_EXCHANGER_Pin GPIO_PIN_6
#define AI2_HEAT_EXCHANGER_GPIO_Port GPIOA
#define DI13_FUEL_STOP_SOLENOID_Pin GPIO_PIN_7
#define DI13_FUEL_STOP_SOLENOID_GPIO_Port GPIOA
#define CAN_ACTIVE_Pin GPIO_PIN_0
#define CAN_ACTIVE_GPIO_Port GPIOB
#define CAN_MUTE_Pin GPIO_PIN_1
#define CAN_MUTE_GPIO_Port GPIOB
#define OW2_CALORIFIER_Pin GPIO_PIN_2
#define OW2_CALORIFIER_GPIO_Port GPIOB
#define BTN0_Pin GPIO_PIN_10
#define BTN0_GPIO_Port GPIOB
#define BTN1_Pin GPIO_PIN_11
#define BTN1_GPIO_Port GPIOB
#define OW0_DRY_EXHAUST_Pin GPIO_PIN_12
#define OW0_DRY_EXHAUST_GPIO_Port GPIOB
#define OW1_GEARBOX_OIL_Pin GPIO_PIN_13
#define OW1_GEARBOX_OIL_GPIO_Port GPIOB
#define DI22_PREHEATER_RELAY_Pin GPIO_PIN_14
#define DI22_PREHEATER_RELAY_GPIO_Port GPIOB
#define HOST_POWER_Pin GPIO_PIN_15
#define HOST_POWER_GPIO_Port GPIOB
#define DO0_PREHEATER_Pin GPIO_PIN_8
#define DO0_PREHEATER_GPIO_Port GPIOA
#define DO1_STARTER_Pin GPIO_PIN_9
#define DO1_STARTER_GPIO_Port GPIOA
#define DO2_STOP_SOLENOID_Pin GPIO_PIN_10
#define DO2_STOP_SOLENOID_GPIO_Port GPIOA
#define CAN_RX_Pin GPIO_PIN_11
#define CAN_RX_GPIO_Port GPIOA
#define CAN_TX_Pin GPIO_PIN_12
#define CAN_TX_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define LED_GREEN_Pin GPIO_PIN_15
#define LED_GREEN_GPIO_Port GPIOA
#define TRACESWO_Pin GPIO_PIN_3
#define TRACESWO_GPIO_Port GPIOB
#define DI20_GEAR_REVERSE_Pin GPIO_PIN_4
#define DI20_GEAR_REVERSE_GPIO_Port GPIOB
#define DI21_GEAR_FORWARD_Pin GPIO_PIN_5
#define DI21_GEAR_FORWARD_GPIO_Port GPIOB
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
