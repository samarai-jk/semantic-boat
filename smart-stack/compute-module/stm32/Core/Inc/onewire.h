#ifndef ONEWIRE_H
#define ONEWIRE_H

#include "stm32f3xx_hal.h"

void OneWire_Init(void);
float DS18B20_ReadTemp(void);
uint8_t DS18B20_Start(void);
float DS18B20_Read(void);

#endif
