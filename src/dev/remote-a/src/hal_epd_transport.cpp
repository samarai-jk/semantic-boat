#include "hal_epd_transport.hpp"
extern "C" {
#include "main.h"
extern SPI_HandleTypeDef hspi1;
}

namespace remote_a {
namespace {
bool transmit(const std::uint8_t* data, std::size_t size, GPIO_PinState dc) {
    if (!data || size == 0u || size > 0xffffu) return false;
    HAL_GPIO_WritePin(DISP_DC_GPIO_Port, DISP_DC_Pin, dc);
    HAL_GPIO_WritePin(DISP_CS_GPIO_Port, DISP_CS_Pin, GPIO_PIN_RESET);
    const auto result = HAL_SPI_Transmit(&hspi1, const_cast<std::uint8_t*>(data),
                                         static_cast<std::uint16_t>(size), 30000u);
    HAL_GPIO_WritePin(DISP_CS_GPIO_Port, DISP_CS_Pin, GPIO_PIN_SET);
    return result == HAL_OK;
}
} // namespace

bool HalEpdTransport::writeCommand(std::uint8_t command) { return transmit(&command, 1u, GPIO_PIN_RESET); }
bool HalEpdTransport::writeData(const std::uint8_t* data, std::size_t size) { return transmit(data, size, GPIO_PIN_SET); }

void HalEpdTransport::reset() {
    HAL_GPIO_WritePin(DISP_RST_GPIO_Port, DISP_RST_Pin, GPIO_PIN_SET); HAL_Delay(100u);
    HAL_GPIO_WritePin(DISP_RST_GPIO_Port, DISP_RST_Pin, GPIO_PIN_RESET); HAL_Delay(100u);
    HAL_GPIO_WritePin(DISP_RST_GPIO_Port, DISP_RST_Pin, GPIO_PIN_SET); HAL_Delay(100u);
}

void HalEpdTransport::power(bool enabled) {
    HAL_GPIO_WritePin(DISP_PWR_GPIO_Port, DISP_PWR_Pin, enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DISP_CS_GPIO_Port, DISP_CS_Pin, GPIO_PIN_SET);
}

bool HalEpdTransport::busy() const { return HAL_GPIO_ReadPin(DISP_BUSY_GPIO_Port, DISP_BUSY_Pin) == GPIO_PIN_SET; }
void HalEpdTransport::delayMs(std::uint32_t duration) { HAL_Delay(duration); }
std::uint32_t HalEpdTransport::millis() const { return HAL_GetTick(); }

} // namespace remote_a
