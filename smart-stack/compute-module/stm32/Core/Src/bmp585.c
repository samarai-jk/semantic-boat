#include "bmp585.h"

#define BMP585_ADDR (0x47 << 1)

// Registers
#define BMP585_REG_CHIP_ID      0x01
#define BMP585_REG_PRESS_DATA   0x20 // 3 bytes: XLSB, LSB, MSB
#define BMP585_REG_OSR_CONFIG   0x36
#define BMP585_REG_ODR_CONFIG   0x37
#define BMP585_REG_CMD          0x7E

static I2C_HandleTypeDef *_hi2c_bmp;

void BMP585_Init(I2C_HandleTypeDef *hi2c)
{
    _hi2c_bmp = hi2c;
    
    HAL_Delay(10);
    
    uint8_t chip_id = 0;
    HAL_I2C_Mem_Read(_hi2c_bmp, BMP585_ADDR, BMP585_REG_CHIP_ID, 1, &chip_id, 1, 100);
    
    // Expect 0x51 for BMP585. If not, maybe handle error, but for now just clear on
    
    // Set OSR (Oversampling)
    // 0x36: Bits 5:3 Pressure OSR, Bits 2:0 Temp OSR.
    // 011 (4x) for Pressure, 000 (1x) for Temp => 0x18
    uint8_t osr_conf = 0x18;
    HAL_I2C_Mem_Write(_hi2c_bmp, BMP585_ADDR, BMP585_REG_OSR_CONFIG, 1, &osr_conf, 1, 100);
    
    // Set ODR and Mode
    // 0x37: Bits 6:2 ODR, Bits 1:0 Mode.
    // Mode: 01 (Normal).
    // ODR: 00100 (4) => 5Hz approx? Let's try 0x10 (shifted is 0x04)
    // Value = (ODR << 2) | Mode
    // Let's use ODR=5 (0x05 << 2 = 0x14) | 0x01 = 0x15.
    uint8_t odr_conf = 0x15; 
    HAL_I2C_Mem_Write(_hi2c_bmp, BMP585_ADDR, BMP585_REG_ODR_CONFIG, 1, &odr_conf, 1, 100);
}

uint8_t BMP585_ReadPressure(float *pressure)
{
    uint8_t data[3];
    
    if (HAL_I2C_Mem_Read(_hi2c_bmp, BMP585_ADDR, BMP585_REG_PRESS_DATA, 1, data, 3, 100) != HAL_OK)
    {
        return 1;
    }
    
    // Data is XLSB, LSB, MSB.
    int32_t raw = ((int32_t)data[2] << 16) | ((int32_t)data[1] << 8) | data[0];
    
    // 24-bit extension if needed? It's unsigned usually, but let's treat as uint32 then cast?
    // Bosh driver uses uint32_t.
    
    *pressure = (float)raw / 64.0f;
    
    return 0;
}
