#include "dht20.h"

#define DHT20_ADDR (0x38 << 1)

static I2C_HandleTypeDef *_hi2c;

void DHT20_Init(I2C_HandleTypeDef *hi2c)
{
    _hi2c = hi2c;
    // Wake up / Initialization if needed, mostly handled by waiting 100ms after power up
    HAL_Delay(100); 
}

uint8_t DHT20_Read(DHT20_Data_t *data)
{
    uint8_t cmd[3] = {0xAC, 0x33, 0x00};
    uint8_t buffer[7];
    
    // Trigger measurement
    if (HAL_I2C_Master_Transmit(_hi2c, DHT20_ADDR, cmd, 3, 100) != HAL_OK)
    {
        return 1; // Error
    }
    
    // Wait for measurement
    HAL_Delay(80);
    
    // Read data
    if (HAL_I2C_Master_Receive(_hi2c, DHT20_ADDR, buffer, 7, 100) != HAL_OK)
    {
        return 2; // Error
    }
    
    // Check status bit (Bit 7 of Byte 0)
    if ((buffer[0] & 0x80) == 0) 
    {
        // Not calibrated or busy?
        // Datasheet says Bit 7 should be 0 for ready? No, "Bit 7: 1 indicates busy"
        // Actually DHT20:
        // Byte 0: State. Bit 7 = Busy indication. 1=Busy, 0=Free.
        // Bit 3 = Calibrated. 1=Calibrated, 0=Not.
        // Wait, normally we check the Busy bit.
       if (buffer[0] & 0x80) return 3; // Busy
    }
    
    uint32_t rawHum = ((uint32_t)buffer[1] << 12) | ((uint32_t)buffer[2] << 4) | ((buffer[3] >> 4) & 0x0F);
    uint32_t rawTemp = (((uint32_t)buffer[3] & 0x0F) << 16) | ((uint32_t)buffer[4] << 8) | buffer[5];
    
    data->Humidity = (float)rawHum / 1048576.0f * 100.0f;
    data->Temperature = ((float)rawTemp / 1048576.0f * 200.0f) - 50.0f;
    
    return 0; // OK
}
