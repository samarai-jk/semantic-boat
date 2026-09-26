#include "drivers/device_driver.hpp"

namespace app {

void DeviceDriver::init() {
    vs_in_ = (HAL_GPIO_ReadPin(MCU_DIG_DEV_VS_IN_GPIO_Port, MCU_DIG_DEV_VS_IN_Pin) == GPIO_PIN_SET);
    vs_out_ = (HAL_GPIO_ReadPin(MCU_DIG_DEV_VS_OUT_GPIO_Port, MCU_DIG_DEV_VS_OUT_Pin) == GPIO_PIN_SET);
}

void DeviceDriver::run() {
    vs_in_ = (HAL_GPIO_ReadPin(MCU_DIG_DEV_VS_IN_GPIO_Port, MCU_DIG_DEV_VS_IN_Pin) == GPIO_PIN_SET);
    vs_out_ = (HAL_GPIO_ReadPin(MCU_DIG_DEV_VS_OUT_GPIO_Port, MCU_DIG_DEV_VS_OUT_Pin) == GPIO_PIN_SET);
}

} // namespace app
