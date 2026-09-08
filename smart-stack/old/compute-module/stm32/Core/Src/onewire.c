#include "onewire.h"
#include "main.h"

// Define the pin and port
#define OW_PIN  MCU_1WIRE_0_Pin
#define OW_PORT MCU_1WIRE_0_GPIO_Port
#define OW_PIN_NUMBER 2 // PE2

static void delay_us(uint32_t us)
{
    // Enable DWT if not enabled
    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    
    // Calculate ticks (SystemCoreClock is usually updated by SystemClock_Config)
    // Using HAL_RCC_GetHCLKFreq() is safer
    uint32_t ticks = us * (HAL_RCC_GetHCLKFreq() / 1000000);
    uint32_t startTick = DWT->CYCCNT;
    
    while ((DWT->CYCCNT - startTick) < ticks);
}

static void OW_SetInput(void)
{
    // PE2: MODER bits 4 and 5 -> 00 for Input
    OW_PORT->MODER &= ~(3UL << (OW_PIN_NUMBER * 2));
}

static void OW_SetOutput(void)
{
    // PE2: MODER bits 4 and 5 -> 01 for Output
    // Ensure we clear first
    OW_PORT->MODER &= ~(3UL << (OW_PIN_NUMBER * 2));
    OW_PORT->MODER |= (1UL << (OW_PIN_NUMBER * 2));
}

void OneWire_Init(void)
{
    // Enable DWT for delays
    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    
    // Ensure output drive is low when we switch to output
    OW_PORT->BRR = OW_PIN;
    
    OW_SetInput(); // Start released (High Z)
}

static uint8_t OneWire_Reset(void)
{
    uint8_t r;
    
    // Drive Low
    OW_SetOutput();
    // Already Low because BRR was set in Init and we only toggle Mode? 
    // Just to be safe, write Low.
    OW_PORT->BRR = OW_PIN; 
    
    delay_us(480);
    
    // Release
    OW_SetInput();
    
    delay_us(70);
    
    // Read
    if ((OW_PORT->IDR & OW_PIN) == 0) r = 0; else r = 1;
    
    delay_us(410);
    
    return r; // 0 if device present (pulled low)
}

static void OneWire_WriteBit(uint8_t bit)
{
    if (bit)
    {
        // Write 1
        OW_SetOutput();
        OW_PORT->BRR = OW_PIN;
        delay_us(6);
        OW_SetInput();
        delay_us(64);
    }
    else
    {
        // Write 0
        OW_SetOutput();
        OW_PORT->BRR = OW_PIN;
        delay_us(60);
        OW_SetInput();
        delay_us(10);
    }
}

static uint8_t OneWire_ReadBit(void)
{
    uint8_t bit = 0;
    
    OW_SetOutput();
    OW_PORT->BRR = OW_PIN;
    delay_us(6);
    OW_SetInput();
    delay_us(9);
    
    if ((OW_PORT->IDR & OW_PIN)) bit = 1;
    
    delay_us(55);
    return bit;
}

static void OneWire_WriteByte(uint8_t byte)
{
    for (int i = 0; i < 8; i++)
    {
        OneWire_WriteBit(byte & 0x01);
        byte >>= 1;
    }
}

static uint8_t OneWire_ReadByte(void)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++)
    {
        if (OneWire_ReadBit()) byte |= (1 << i);
    }
    return byte;
}

uint8_t DS18B20_Start(void)
{
    __disable_irq();
    uint8_t presence = OneWire_Reset();
    __enable_irq();

    if (presence != 0) return 1; // Error

    __disable_irq();
    OneWire_WriteByte(0xCC); // Skip ROM
    OneWire_WriteByte(0x44); // Convert T
    __enable_irq();
    
    return 0;
}

float DS18B20_Read(void)
{
    __disable_irq();
    uint8_t presence = OneWire_Reset();
    __enable_irq();
    
    if (presence != 0) return -999.0f;
    
    __disable_irq();
    OneWire_WriteByte(0xCC); // Skip ROM
    OneWire_WriteByte(0xBE); // Read Scratchpad
    
    uint8_t low = OneWire_ReadByte();
    uint8_t high = OneWire_ReadByte();
    __enable_irq();
    
    int16_t val = (high << 8) | low;
    
    return (float)val / 16.0f;
}

float DS18B20_ReadTemp(void)
{
    // Need to disable interrupts during timing critical sections?
    // With 1-Wire, YES, usually.
    __disable_irq();
    uint8_t presence = OneWire_Reset();
    __enable_irq();

    if (presence != 0) return -999.0f; // No device responding
    
    __disable_irq();
    OneWire_WriteByte(0xCC); // Skip ROM
    OneWire_WriteByte(0x44); // Convert T
    __enable_irq();
    
    // Conversion Wait (750ms for 12bit)
    HAL_Delay(750);
    
    __disable_irq();
    presence = OneWire_Reset();
    __enable_irq();
    
    if (presence != 0) return -999.0f;
    
    __disable_irq();
    OneWire_WriteByte(0xCC); // Skip ROM
    OneWire_WriteByte(0xBE); // Read Scratchpad
    
    uint8_t low = OneWire_ReadByte();
    uint8_t high = OneWire_ReadByte();
    __enable_irq();
    
    int16_t val = (high << 8) | low;
    
    return (float)val / 16.0f;
}
