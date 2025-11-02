/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f3xx_hal.h"

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
#define UNUSEDC14_Pin GPIO_PIN_14
#define UNUSEDC14_GPIO_Port GPIOC
#define UNUSEDC15_Pin GPIO_PIN_15
#define UNUSEDC15_GPIO_Port GPIOC
#define RELAY_ON_Pin GPIO_PIN_0
#define RELAY_ON_GPIO_Port GPIOA
#define RELAY_OFF_Pin GPIO_PIN_1
#define RELAY_OFF_GPIO_Port GPIOA
#define TEMP_SENSE_ON_Pin GPIO_PIN_4
#define TEMP_SENSE_ON_GPIO_Port GPIOA
#define MCU_DIG_DEV_VS_IN_Pin GPIO_PIN_5
#define MCU_DIG_DEV_VS_IN_GPIO_Port GPIOA
#define MCU_DIG_DEV_VS_OUT_Pin GPIO_PIN_6
#define MCU_DIG_DEV_VS_OUT_GPIO_Port GPIOA
#define FEEDBACK_LED_Pin GPIO_PIN_0
#define FEEDBACK_LED_GPIO_Port GPIOB
#define MCU_ANA_DEV_V_Pin GPIO_PIN_1
#define MCU_ANA_DEV_V_GPIO_Port GPIOB
#define MCU_ANA_DEV_I_Pin GPIO_PIN_2
#define MCU_ANA_DEV_I_GPIO_Port GPIOB
#define UNUSEDE8_Pin GPIO_PIN_8
#define UNUSEDE8_GPIO_Port GPIOE
#define MCU_ANA_DEV_T_Pin GPIO_PIN_9
#define MCU_ANA_DEV_T_GPIO_Port GPIOE
#define UNUSEDB14_Pin GPIO_PIN_14
#define UNUSEDB14_GPIO_Port GPIOB
#define UNUSEDB15_Pin GPIO_PIN_15
#define UNUSEDB15_GPIO_Port GPIOB
#define UNUSEDD8_Pin GPIO_PIN_8
#define UNUSEDD8_GPIO_Port GPIOD
#define MCU_TOGGLE_Pin GPIO_PIN_9
#define MCU_TOGGLE_GPIO_Port GPIOA
#define MCU_TOGGLE_EXTI_IRQn EXTI9_5_IRQn
#define MCU_FEEDBACK_Pin GPIO_PIN_10
#define MCU_FEEDBACK_GPIO_Port GPIOA
#define IND_LED_BLU_Pin GPIO_PIN_11
#define IND_LED_BLU_GPIO_Port GPIOA
#define CUR_SENSE_ON_Pin GPIO_PIN_12
#define CUR_SENSE_ON_GPIO_Port GPIOA
#define IND_LED_RED_Pin GPIO_PIN_6
#define IND_LED_RED_GPIO_Port GPIOF
#define UNUSEDF7_Pin GPIO_PIN_7
#define UNUSEDF7_GPIO_Port GPIOF
#define BTN_FUNCTION_Pin GPIO_PIN_15
#define BTN_FUNCTION_GPIO_Port GPIOA
#define BTN_FUNCTION_EXTI_IRQn EXTI15_10_IRQn
#define BTN_TOGGLE_Pin GPIO_PIN_4
#define BTN_TOGGLE_GPIO_Port GPIOB
#define BTN_TOGGLE_EXTI_IRQn EXTI4_IRQn
#define IND_LED_GRN_Pin GPIO_PIN_8
#define IND_LED_GRN_GPIO_Port GPIOB
#define MCU_ERROR_Pin GPIO_PIN_9
#define MCU_ERROR_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
