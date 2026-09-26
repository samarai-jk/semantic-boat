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
#define MCU_1WIRE_0_Pin GPIO_PIN_2
#define MCU_1WIRE_0_GPIO_Port GPIOE
#define MCU_1WIRE_1_Pin GPIO_PIN_3
#define MCU_1WIRE_1_GPIO_Port GPIOE
#define MCU_1WIRE_2_Pin GPIO_PIN_4
#define MCU_1WIRE_2_GPIO_Port GPIOE
#define MCU_1WIRE_3_Pin GPIO_PIN_5
#define MCU_1WIRE_3_GPIO_Port GPIOE
#define GPIO_0_Pin GPIO_PIN_6
#define GPIO_0_GPIO_Port GPIOE
#define UNUSED_Pin GPIO_PIN_13
#define UNUSED_GPIO_Port GPIOC
#define GPIO_1_Pin GPIO_PIN_9
#define GPIO_1_GPIO_Port GPIOF
#define GPIO_2_Pin GPIO_PIN_10
#define GPIO_2_GPIO_Port GPIOF
#define UNUSEDC0_Pin GPIO_PIN_0
#define UNUSEDC0_GPIO_Port GPIOC
#define UNUSEDC1_Pin GPIO_PIN_1
#define UNUSEDC1_GPIO_Port GPIOC
#define UNUSEDC2_Pin GPIO_PIN_2
#define UNUSEDC2_GPIO_Port GPIOC
#define UNUSEDC3_Pin GPIO_PIN_3
#define UNUSEDC3_GPIO_Port GPIOC
#define GPIO_3_Pin GPIO_PIN_2
#define GPIO_3_GPIO_Port GPIOF
#define STATUS_LED_RED_Pin GPIO_PIN_0
#define STATUS_LED_RED_GPIO_Port GPIOA
#define STATUS_LED_GRN_Pin GPIO_PIN_1
#define STATUS_LED_GRN_GPIO_Port GPIOA
#define STATUS_LED_BLU_Pin GPIO_PIN_2
#define STATUS_LED_BLU_GPIO_Port GPIOA
#define PWM_OUT_2_Pin GPIO_PIN_3
#define PWM_OUT_2_GPIO_Port GPIOA
#define UNUSEDF4_Pin GPIO_PIN_4
#define UNUSEDF4_GPIO_Port GPIOF
#define DAC_OUT_0_Pin GPIO_PIN_4
#define DAC_OUT_0_GPIO_Port GPIOA
#define SIG_LED_0_Pin GPIO_PIN_6
#define SIG_LED_0_GPIO_Port GPIOA
#define SIG_LED_1_Pin GPIO_PIN_7
#define SIG_LED_1_GPIO_Port GPIOA
#define STL_COM_RX_Pin GPIO_PIN_4
#define STL_COM_RX_GPIO_Port GPIOC
#define STL_COM_TX_Pin GPIO_PIN_5
#define STL_COM_TX_GPIO_Port GPIOC
#define ADIF_P_3_Pin GPIO_PIN_0
#define ADIF_P_3_GPIO_Port GPIOB
#define ADIF_N_3_Pin GPIO_PIN_1
#define ADIF_N_3_GPIO_Port GPIOB
#define ADIF_P_2_Pin GPIO_PIN_2
#define ADIF_P_2_GPIO_Port GPIOB
#define ADIF_N_2_Pin GPIO_PIN_7
#define ADIF_N_2_GPIO_Port GPIOE
#define UNUSEDE8_Pin GPIO_PIN_8
#define UNUSEDE8_GPIO_Port GPIOE
#define UNUSEDE9_Pin GPIO_PIN_9
#define UNUSEDE9_GPIO_Port GPIOE
#define ADIF_P_1_Pin GPIO_PIN_10
#define ADIF_P_1_GPIO_Port GPIOE
#define ADIF_N_1_Pin GPIO_PIN_11
#define ADIF_N_1_GPIO_Port GPIOE
#define ADIF_P_0_Pin GPIO_PIN_12
#define ADIF_P_0_GPIO_Port GPIOE
#define ADIF_N_0_Pin GPIO_PIN_13
#define ADIF_N_0_GPIO_Port GPIOE
#define UNUSEDE14_Pin GPIO_PIN_14
#define UNUSEDE14_GPIO_Port GPIOE
#define UNUSEDE15_Pin GPIO_PIN_15
#define UNUSEDE15_GPIO_Port GPIOE
#define UNUSEDB10_Pin GPIO_PIN_10
#define UNUSEDB10_GPIO_Port GPIOB
#define UNUSEDB14_Pin GPIO_PIN_14
#define UNUSEDB14_GPIO_Port GPIOB
#define UNUSEDB15_Pin GPIO_PIN_15
#define UNUSEDB15_GPIO_Port GPIOB
#define ADIF_P_7_Pin GPIO_PIN_8
#define ADIF_P_7_GPIO_Port GPIOD
#define ADIF_N_7_Pin GPIO_PIN_9
#define ADIF_N_7_GPIO_Port GPIOD
#define ADIF_P_6_Pin GPIO_PIN_10
#define ADIF_P_6_GPIO_Port GPIOD
#define ADIF_N_6_Pin GPIO_PIN_11
#define ADIF_N_6_GPIO_Port GPIOD
#define ADIF_P_5_Pin GPIO_PIN_12
#define ADIF_P_5_GPIO_Port GPIOD
#define ADIF_N_5_Pin GPIO_PIN_13
#define ADIF_N_5_GPIO_Port GPIOD
#define ADIF_P_4_Pin GPIO_PIN_14
#define ADIF_P_4_GPIO_Port GPIOD
#define ADIF_N_4_Pin GPIO_PIN_15
#define ADIF_N_4_GPIO_Port GPIOD
#define BTN_FUNCTION_0_Pin GPIO_PIN_6
#define BTN_FUNCTION_0_GPIO_Port GPIOC
#define BTN_FUNCTION_1_Pin GPIO_PIN_7
#define BTN_FUNCTION_1_GPIO_Port GPIOC
#define PWM_OUT_0_Pin GPIO_PIN_8
#define PWM_OUT_0_GPIO_Port GPIOC
#define PWM_OUT_1_Pin GPIO_PIN_9
#define PWM_OUT_1_GPIO_Port GPIOC
#define MCU_STACK_ENABLE_Pin GPIO_PIN_8
#define MCU_STACK_ENABLE_GPIO_Port GPIOA
#define MCU_I2C_SCL_Pin GPIO_PIN_9
#define MCU_I2C_SCL_GPIO_Port GPIOA
#define MCU_I2C_SDA_Pin GPIO_PIN_10
#define MCU_I2C_SDA_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SPI1_MOSI_Pin GPIO_PIN_6
#define SPI1_MOSI_GPIO_Port GPIOF
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define MCU_SMB_CLK_Pin GPIO_PIN_15
#define MCU_SMB_CLK_GPIO_Port GPIOA
#define MCU_DIG_IN_7_Pin GPIO_PIN_10
#define MCU_DIG_IN_7_GPIO_Port GPIOC
#define MCU_DIG_IN_6_Pin GPIO_PIN_11
#define MCU_DIG_IN_6_GPIO_Port GPIOC
#define MCU_DIG_IN_5_Pin GPIO_PIN_12
#define MCU_DIG_IN_5_GPIO_Port GPIOC
#define MCU_DIG_IN_4_Pin GPIO_PIN_0
#define MCU_DIG_IN_4_GPIO_Port GPIOD
#define MCU_DIG_IN_3_Pin GPIO_PIN_1
#define MCU_DIG_IN_3_GPIO_Port GPIOD
#define MCU_DIG_IN_2_Pin GPIO_PIN_2
#define MCU_DIG_IN_2_GPIO_Port GPIOD
#define MCU_DIG_IN_1_Pin GPIO_PIN_3
#define MCU_DIG_IN_1_GPIO_Port GPIOD
#define MCU_DIG_IN_0_Pin GPIO_PIN_4
#define MCU_DIG_IN_0_GPIO_Port GPIOD
#define UNUSEDD7_Pin GPIO_PIN_7
#define UNUSEDD7_GPIO_Port GPIOD
#define TRACESWO_Pin GPIO_PIN_3
#define TRACESWO_GPIO_Port GPIOB
#define SPI1_MISO_Pin GPIO_PIN_4
#define SPI1_MISO_GPIO_Port GPIOB
#define MCU_SMB_ALERT_Pin GPIO_PIN_5
#define MCU_SMB_ALERT_GPIO_Port GPIOB
#define UNUSEDB6_Pin GPIO_PIN_6
#define UNUSEDB6_GPIO_Port GPIOB
#define MCU_SMB_DATA_Pin GPIO_PIN_7
#define MCU_SMB_DATA_GPIO_Port GPIOB
#define UNUSEDB8_Pin GPIO_PIN_8
#define UNUSEDB8_GPIO_Port GPIOB
#define UNUSEDB9_Pin GPIO_PIN_9
#define UNUSEDB9_GPIO_Port GPIOB
#define UNUSEDE0_Pin GPIO_PIN_0
#define UNUSEDE0_GPIO_Port GPIOE
#define UNUSEDE1_Pin GPIO_PIN_1
#define UNUSEDE1_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
