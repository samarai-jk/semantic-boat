#include "can_mcu/supervision.hpp"
extern "C" {
#include "main.h"
}

namespace can_mcu {
namespace {

constexpr std::uint32_t iwdgStart = 0xCCCCu;
constexpr std::uint32_t iwdgUnlock = 0x5555u;
constexpr std::uint32_t iwdgRefresh = 0xAAAAu;
constexpr std::uint32_t iwdgPrescaler64 = 4u; // 32 kHz / 64 = 500 Hz
constexpr std::uint32_t crashMagic = 0xC2A5'4001u;

bool watchdogStarted{};
volatile std::uint32_t drops{};

void enableBackupAccess() {
    __HAL_RCC_PWR_CLK_ENABLE();
    PWR->CR1 |= PWR_CR1_DBP;
}

} // namespace

void startWatchdog(std::uint32_t timeoutMs) {
    auto reload = timeoutMs / 2u; // 500 Hz
    if (reload < 1u) reload = 1u;
    if (reload > 0xFFFu) reload = 0xFFFu;
    DBGMCU->APB1FZR1 |= DBGMCU_APB1FZR1_DBG_IWDG_STOP;
    IWDG->KR = iwdgStart;
    IWDG->KR = iwdgUnlock;
    IWDG->PR = iwdgPrescaler64;
    IWDG->RLR = reload;
    for (std::uint32_t spins = 0u; IWDG->SR != 0u && spins < 1000000u; ++spins) {
    }
    IWDG->KR = iwdgRefresh;
    watchdogStarted = true;
}

void refreshWatchdog() {
    if (watchdogStarted) IWDG->KR = iwdgRefresh;
}

bool watchdogRunning() {
    return watchdogStarted;
}

void startPowerMonitor(PowerThreshold threshold) {
    __HAL_RCC_PWR_CLK_ENABLE();
    PWR->CR2 = (PWR->CR2 & ~PWR_CR2_PLS) |
               (static_cast<std::uint32_t>(threshold) << PWR_CR2_PLS_Pos) | PWR_CR2_PVDE;
    // PVDO rises when VDD falls below the threshold.
    EXTI->RTSR1 |= EXTI_RTSR1_RT16;
    EXTI->PR1 = EXTI_PR1_PIF16;
    EXTI->IMR1 |= EXTI_IMR1_IM16;
    HAL_NVIC_SetPriority(PVD_PVM_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(PVD_PVM_IRQn);
}

std::uint32_t powerDrops() {
    return drops;
}

bool powerLow() {
    return (PWR->SR2 & PWR_SR2_PVDO) != 0u;
}

CrashRecord takeCrashRecord() {
    enableBackupAccess();
    CrashRecord record{};
    if (RTC->BKP0R == crashMagic) {
        record.kind = static_cast<CrashRecord::Kind>(RTC->BKP1R);
        record.uptimeMs = RTC->BKP2R;
        record.cfsr = RTC->BKP3R;
        record.hfsr = RTC->BKP4R;
        record.bfar = RTC->BKP5R;
        record.mmfar = RTC->BKP6R;
    }
    RTC->BKP0R = 0u;
    return record;
}

// Called from the fatal path with interrupts disabled.
void storeCrashRecord(CrashRecord::Kind kind) {
    enableBackupAccess();
    RTC->BKP1R = static_cast<std::uint32_t>(kind);
    RTC->BKP2R = HAL_GetTick();
    RTC->BKP3R = SCB->CFSR;
    RTC->BKP4R = SCB->HFSR;
    RTC->BKP5R = SCB->BFAR;
    RTC->BKP6R = SCB->MMFAR;
    RTC->BKP0R = crashMagic;
}

} // namespace can_mcu

extern "C" void PVD_PVM_IRQHandler(void) {
    if (EXTI->PR1 & EXTI_PR1_PIF16) {
        EXTI->PR1 = EXTI_PR1_PIF16;
        can_mcu::drops = can_mcu::drops + 1u;
    }
}
