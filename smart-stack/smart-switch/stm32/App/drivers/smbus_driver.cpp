#include "drivers/smbus_driver.hpp"
#include "events.hpp"
#include "log.hpp"
#include "stm32f3xx_ll_i2c.h"
#include "lib.hpp"

extern "C"
{
#include "main.h"
    extern SMBUS_HandleTypeDef hsmbus1;
}

using namespace app;

static SmBusDriver *s_inst = nullptr;
SmBusDriver *SmBusDriver::instance() { return s_inst; }

void SmBusDriver::init()
{
    
    // The driver needs to be re-written, was just a prove of concept. It worked in master mode, but
    // needs to be completely rewritten TIP: ONLY TEST WITH A PAIR OF DEVICES! The bus does not seem
    // to do anything if there's not at least two devices, not possible to properly debug
    
    // s_inst = this;
    // busy_ = false;
    
    // // Check if SMBus peripheral is ready
    // swo_trace_linef(log_level_t::LOG_INFO, "SMBus init - State: %d, ErrorCode: 0x%08lX", 
    //            hsmbus1.State, hsmbus1.ErrorCode);
    
    // // Initialize as I2C (not SMBUS) for simpler operation
    // // We'll cast and reinitialize the peripheral
    // I2C_HandleTypeDef* hi2c = (I2C_HandleTypeDef*)&hsmbus1;
    
    // // Deinit SMBUS first
    // HAL_SMBUS_DeInit(&hsmbus1);
    
    // // Now init as regular I2C
    // hi2c->Instance = I2C1;
    // hi2c->Init.Timing = 0x00201D2B;  // Same timing as SMBUS
    // hi2c->Init.OwnAddress1 = 0;
    // hi2c->Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    // hi2c->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    // hi2c->Init.OwnAddress2 = 0;
    // hi2c->Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    // hi2c->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    // hi2c->Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    
    // if (HAL_I2C_Init(hi2c) != HAL_OK)
    // {
    //     swo_trace_line("I2C init failed!");
    //     Error_Handler();
    // }
    
    // swo_trace_linef(log_level_t::LOG_INFO, "I2C initialized - State: %d", hi2c->State);
    
    // // Enable test mode by default - now using POLLING mode (safe, no interrupts)
    // enableTestMode(true, 500);
    
    // swo_trace_line("I2C master driver initialized (POLLING mode) - test enabled");
}

void SmBusDriver::run()
{
    // static bool initialized = false;
    // if (!initialized)
    // {
    //     initialized = true;
    //     swo_trace_line("SMBus run() started");
    //     return; // Skip first call to let system stabilize
    // }
    
    // // Heartbeat to verify run() is being called
    // static uint32_t last_heartbeat = 0;
    // uint32_t now = HAL_GetTick();
    
    // if ((now - last_heartbeat) >= 5000)
    // {
    //     last_heartbeat = now;
    //     I2C_HandleTypeDef* hi2c = (I2C_HandleTypeDef*)&hsmbus1;
    //     swo_trace_linef(LOG_INFO, "Heartbeat - I2C state=%ld, busy=%d", (uint32_t)hi2c->State, busy_);
    // }
    
    // // I2C master test mode: periodically send test data to slave address 0x50
    // if (test_mode_)
    // {
    //     if ((now - last_test_tick_) >= test_period_ms_)
    //     {
    //         last_test_tick_ = now;
            
    //         I2C_HandleTypeDef* hi2c = (I2C_HandleTypeDef*)&hsmbus1;
            
    //         // Force reset if peripheral is not ready
    //         if (hi2c->State != HAL_I2C_STATE_READY)
    //         {
    //             hi2c->ErrorCode = HAL_I2C_ERROR_NONE;
    //             hi2c->State = HAL_I2C_STATE_READY;
    //             busy_ = false;
    //         }
            
    //         // Send incrementing test pattern
    //         uint8_t test_data[4] = {
    //             test_counter_,
    //             static_cast<uint8_t>(test_counter_ + 1),
    //             static_cast<uint8_t>(test_counter_ + 2),
    //             static_cast<uint8_t>(test_counter_ + 3)
    //         };
            
    //         bool success = masterWrite(0x50, test_data, 4);
    //         test_counter_++;
            
    //         if (test_counter_ % 5 == 0)  // Log every 5th
    //         {
    //             swo_trace_linef(LOG_INFO, "I2C TX #%d: %s", test_counter_, success ? "OK" : "FAIL");
    //         }
    //     }
    // }
}

bool SmBusDriver::masterWrite(uint8_t slaveAddr, const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || len > kBufSize)
        return false;
    if (busy_)
        return false;
    
    // Copy to internal buffer
    for (uint16_t i = 0; i < len; ++i)
        tx_buf_[i] = data[i];
    
    // Use I2C handle in POLLING mode (blocking, no interrupts)
    I2C_HandleTypeDef* hi2c = (I2C_HandleTypeDef*)&hsmbus1;
    
    busy_ = true;
    // Timeout of 10ms should be enough for the transaction
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(hi2c, slaveAddr << 1, tx_buf_, len, 10);
    busy_ = false;  // Clear immediately after polling completes
    
    return (status == HAL_OK);
}

bool SmBusDriver::masterRead(uint8_t slaveAddr, uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || len > kBufSize)
        return false;
    if (busy_)
        return false;
    
    // Use I2C handle in POLLING mode (blocking, no interrupts)
    I2C_HandleTypeDef* hi2c = (I2C_HandleTypeDef*)&hsmbus1;
    
    busy_ = true;
    HAL_StatusTypeDef status = HAL_I2C_Master_Receive(hi2c, slaveAddr << 1, rx_buf_, len, 10);
    busy_ = false;
    
    if (status == HAL_OK)
    {
        // Copy received data to user buffer
        for (uint16_t i = 0; i < len; ++i)
            data[i] = rx_buf_[i];
        return true;
    }
    
    return false;
}

bool SmBusDriver::masterWriteRead(uint8_t slaveAddr, const uint8_t *writeData, uint16_t writeLen,
                                   uint8_t *readData, uint16_t readLen)
{
    // For now, implement as two separate transactions
    // A full implementation would use sequential frames with restart condition
    
    if (!writeData || !readData || writeLen == 0 || readLen == 0)
        return false;
    if (writeLen > kBufSize || readLen > kBufSize)
        return false;
    if (busy_)
        return false;
    
    // This is a simplified implementation - write first, then read
    // In production, you'd want a state machine to handle the read after write completes
    bool success = masterWrite(slaveAddr, writeData, writeLen);
    if (!success)
        return false;
    
    // Wait for write to complete (blocking for simplicity)
    uint32_t timeout = HAL_GetTick() + 100;
    while (busy_ && HAL_GetTick() < timeout)
    {
        // Wait
    }
    
    if (busy_)
        return false;
    
    return masterRead(slaveAddr, readData, readLen);
}

void SmBusDriver::enableTestMode(bool en, uint32_t period_ms)
{
    test_mode_ = en;
    test_period_ms_ = period_ms;
    
    if (en)
    {
        swo_trace_linef(LOG_INFO, "SMBus I2C master test enabled - addr 0x50, period %lu ms", period_ms);
    }
    else
    {
        swo_trace_line("SMBus I2C master test disabled");
    }
}

void SmBusDriver::enableGpioTestMode(bool en, uint32_t period_ms)
{
    gpio_test_mode_ = en;
    gpio_test_period_ms_ = period_ms;
    
    if (en)
    {
        configureGpioTestPins();
        swo_trace_linef(LOG_INFO, "GPIO test mode enabled - toggling PB5/PB6/PB7 at %lu ms", period_ms);
    }
    else
    {
        restoreI2cPins();
        swo_trace_line(LOG_INFO, "GPIO test mode disabled - I2C pins restored");
    }
}

void SmBusDriver::onMasterTxComplete()
{
    busy_ = false;
    // Transaction completed successfully
}

void SmBusDriver::onMasterRxComplete()
{
    busy_ = false;
    // Data is now in rx_buf_
}

void SmBusDriver::onError(uint32_t err)
{
    busy_ = false;
    
    I2C_HandleTypeDef* hi2c = (I2C_HandleTypeDef*)&hsmbus1;
    
    // Clear error flags
    __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_BERR);
    __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_ARLO);
    __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_AF);
    __HAL_I2C_CLEAR_FLAG(hi2c, I2C_FLAG_OVR);
    
    // Clear error and force ready
    hi2c->ErrorCode = HAL_I2C_ERROR_NONE;
    hi2c->State = HAL_I2C_STATE_READY;
    
    // Log errors occasionally
    static uint8_t err_count = 0;
    if (++err_count >= 20)
    {
        err_count = 0;
        swo_trace_linef(log_level_t::LOG_INFO, "I2C err: 0x%08lX (NACK expected)", err);
    }
}

void SmBusDriver::configureGpioTestPins()
{
    // Reconfigure PB5 (ALERT), PB6 (SCL), and PB7 (SDA) as GPIO outputs (push-pull)
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // Initialize all pins low
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_RESET);
    scl_state_ = false;
    sda_state_ = false;
}

void SmBusDriver::restoreI2cPins()
{
    // Restore PB5 (ALERT), PB6 (SCL), and PB7 (SDA) to I2C/SMBus alternate function
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;  // Open-drain for I2C/SMBus
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

// C HAL callbacks forwarders
extern "C" void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1 && SmBusDriver::instance())
    {
        SmBusDriver::instance()->onMasterTxComplete();
    }
}

extern "C" void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1 && SmBusDriver::instance())
    {
        SmBusDriver::instance()->onMasterRxComplete();
    }
}

extern "C" void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1 && SmBusDriver::instance())
    {
        SmBusDriver::instance()->onError(HAL_I2C_GetError(hi2c));
    }
}

// Keep SMBUS callbacks too in case they're needed
extern "C" void HAL_SMBUS_MasterTxCpltCallback(SMBUS_HandleTypeDef *hsmbus)
{
    if (hsmbus->Instance == I2C1 && SmBusDriver::instance())
    {
        SmBusDriver::instance()->onMasterTxComplete();
    }
}

extern "C" void HAL_SMBUS_MasterRxCpltCallback(SMBUS_HandleTypeDef *hsmbus)
{
    if (hsmbus->Instance == I2C1 && SmBusDriver::instance())
    {
        SmBusDriver::instance()->onMasterRxComplete();
    }
}

extern "C" void HAL_SMBUS_ErrorCallback(SMBUS_HandleTypeDef *hsmbus)
{
    if (hsmbus->Instance == I2C1 && SmBusDriver::instance())
    {
        SmBusDriver::instance()->onError(HAL_SMBUS_GetError(hsmbus));
    }
}
