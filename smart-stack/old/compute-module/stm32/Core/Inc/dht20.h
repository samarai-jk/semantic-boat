#ifndef DHT20_H
#define DHT20_H

#include "stm32f3xx_hal.h"

typedef struct {
    float Temperature;
    float Humidity;
} DHT20_Data_t;

void DHT20_Init(I2C_HandleTypeDef *hi2c);
uint8_t DHT20_Read(DHT20_Data_t *data);

#endif
