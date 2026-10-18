#include "can_mcu/firmware.hpp"
#include "can_mcu/image_header.hpp"
extern "C" {
#include "main.h"
}

namespace can_mcu {

namespace image = can_mcu::image;

void restartIntoBootloader() {
    PWR->CR1 |= PWR_CR1_DBP;
    RTC->BKP7R = image::bootRequestMagic;
    NVIC_SystemReset();
    while (true) {
    }
}

void confirmFirmware() {
    if ((RTC->BKP8R >> 16u) != image::trialMagic) return;
    PWR->CR1 |= PWR_CR1_DBP;
    RTC->BKP8R = 0u;
}

} // namespace can_mcu
