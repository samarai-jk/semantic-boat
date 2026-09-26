#ifndef BMP585_H
#define BMP585_H

#include "stm32f3xx_hal.h"

void BMP585_Init(I2C_HandleTypeDef *hi2c);
uint8_t BMP585_ReadPressure(float *pressure);

#endif
