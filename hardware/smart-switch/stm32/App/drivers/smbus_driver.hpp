#ifndef APP_DRIVERS_SMBUS_DRIVER_HPP
#define APP_DRIVERS_SMBUS_DRIVER_HPP

#include <cstdint>
#include <cstddef>
#include "driver.hpp"
#include "event_bus.hpp"

extern "C"
{
#include "main.h"
}

namespace app
{

    // SMBus (I2C) master driver using I2C1 in SMBus host mode.
    // - Supports master write/read operations
    // - Test mode for oscilloscope verification (generates I2C traffic)
    class SmBusDriver : public IDriver
    {
    public:
        void init() override;
        void run() override;

        // Master write: send data to slave at specified address
        // Returns false if busy or invalid parameters
        bool masterWrite(uint8_t slaveAddr, const uint8_t *data, uint16_t len);
        
        // Master read: read data from slave at specified address
        // Returns false if busy or invalid parameters
        bool masterRead(uint8_t slaveAddr, uint8_t *data, uint16_t len);
        
        // Master write-then-read (common for register access)
        bool masterWriteRead(uint8_t slaveAddr, const uint8_t *writeData, uint16_t writeLen, 
                            uint8_t *readData, uint16_t readLen);

        // Test mode: enables periodic I2C transactions for oscilloscope verification
        // Sends test pattern to a dummy slave address (requires pull-up resistors)
        void enableTestMode(bool en, uint32_t period_ms = 500);
        
        // GPIO test mode: toggles SCL/SDA pins as GPIO outputs for scope verification
        // Use this if you have no pull-up resistors or want to verify pins work
        void enableGpioTestMode(bool en, uint32_t period_ms = 250);
        
        // Check if driver is currently busy with a transaction
        bool isBusy() const { return busy_; }

        // Singleton accessor (used by C callbacks)
        static SmBusDriver *instance();

        // Internal: called from HAL callbacks
        void onMasterTxComplete();
        void onMasterRxComplete();
        void onError(uint32_t err);

    private:
        static constexpr uint16_t kBufSize = 64;

        uint8_t tx_buf_[kBufSize]{};
        uint8_t rx_buf_[kBufSize]{};
        
        volatile bool busy_{false};
        
        // Test mode variables
        bool test_mode_{false};
        uint32_t test_period_ms_{500};
        uint32_t last_test_tick_{0};
        uint8_t test_counter_{0};
        
        // GPIO test mode variables
        bool gpio_test_mode_{false};
        uint32_t gpio_test_period_ms_{250};
        uint32_t last_gpio_toggle_tick_{0};
        bool scl_state_{false};
        bool sda_state_{false};
        
        void configureGpioTestPins();
        void restoreI2cPins();
    };

} // namespace app

#endif // APP_DRIVERS_SMBUS_DRIVER_HPP
