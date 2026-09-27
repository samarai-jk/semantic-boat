#include "remote_a/app.h"
#include "alarm_service.hpp"
#include "board.hpp"
#include "config_staging.hpp"
#include "configuration_service.hpp"
#include "device_settings.hpp"
#include "display_config.hpp"
#include "display_input_service.hpp"
#include "display_link_service.hpp"
#include "events.hpp"
#include "feedback_service.hpp"
#include "hal_eeprom.hpp"
#include "hal_epd_transport.hpp"
#include "semantic_display/compiler.hpp"
#include "semantic_display/data.hpp"
#include "semantic_display/display.hpp"
#include "semantic_display/history.hpp"
#include "semantic_display/storage.hpp"
#include "setup_service.hpp"
#include "slstm32/application.hpp"
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/at24c256.hpp"
#include "slstm32/drivers/led.hpp"
#include "slstm32/drivers/rgb_led.hpp"
#include "slstm32/drivers/tone.hpp"
#include "slstm32/epd/monochrome_canvas.hpp"
#include "slstm32/epd/waveshare_3in7.hpp"
#include "slstm32/event_bus.hpp"
#include "uart_transport.hpp"
#include <array>
#include <cstdio>
extern "C" {
#include "main.h"
extern RTC_HandleTypeDef hrtc;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
void SystemClock_Config(void);
}

namespace {

std::uint32_t millis() { return HAL_GetTick(); }
volatile std::uint32_t criticalDepth{};
volatile bool buttonInterruptObserved{};
bool sleepPeripheralsSuspended{};
std::uint32_t savedPrimask{};

void enterCritical() {
    const auto primask = __get_PRIMASK();
    __disable_irq();
    if (criticalDepth++ == 0u) savedPrimask = primask;
}

void exitCritical() {
    if (criticalDepth != 0u && --criticalDepth == 0u && savedPrimask == 0u) __enable_irq();
}

const slstm32::Runtime runtime{&millis, &enterCritical, &exitCritical};
slstm32::EventBus events{runtime};
const auto buttonHardware = remote_a::hardware::buttons();
slstm32::drivers::ButtonDriver buttons{runtime, events, remote_a::buttonPressed,
    buttonHardware.data(), buttonHardware.size()};
slstm32::drivers::LedDriver signalLed{runtime, remote_a::hardware::signalLed()};
slstm32::drivers::RgbLedDriver statusLed{runtime, remote_a::hardware::rgbLed()};
slstm32::drivers::ToneDriver buzzer{runtime, remote_a::hardware::buzzer()};
remote_a::HalEpdTransport epdTransport;
slstm32::epd::Waveshare3In7 epdPanel{epdTransport};
std::array<std::uint8_t, slstm32::epd::Waveshare3In7::bufferSize> frameBuffer{};
slstm32::epd::MonochromeCanvas canvas{frameBuffer.data(), frameBuffer.size(),
    slstm32::epd::Waveshare3In7::panelWidth, slstm32::epd::Waveshare3In7::panelHeight,
    slstm32::epd::Rotation::degrees90};

constexpr semantic_display::ConfigStorageLayout configStorageLayout{};
constexpr std::size_t compiledConfigCapacity = configStorageLayout.maxPackageBytes();
// Keep enough RAM1 free for ConfigCompiler, whose section parser currently has
// a roughly 6 KiB stack frame. SWD staging retains its independent 16 KiB
// source limit; only live Semantic Link downloads use this smaller buffer.
constexpr std::size_t downloadedConfigCapacity = 8u * 1024u;
constexpr std::size_t historyCapacity = 1024u;
constexpr std::size_t sourceCapacity = 64u;
// Development firmware uses the embedded fake-data configuration only when no
// persisted or SWD-staged user configuration exists. Set false for a production
// image that should boot the small factory/no-config page instead.
constexpr bool useDevelopmentDisplayConfiguration = false;
// A missing/unwritable EEPROM is useful diagnostic information in production,
// but should not cover the development UI when the embedded test package is
// intentionally allowed to run from RAM.
constexpr bool showDevelopmentStorageWarnings = false;
// At the maximum planned 3 Hz UI rate this is about 30 minutes. Set to zero
// to disable periodic full refreshes entirely.
constexpr std::uint32_t fullRefreshAfterPartialUpdates = 5400u;
__attribute__((section(".config_package")))
std::array<std::uint8_t, compiledConfigCapacity> compiledConfig{};
std::array<char, downloadedConfigCapacity> downloadedConfig{};
std::array<std::uint8_t, historyCapacity> historyMemory{};
std::array<semantic_display::ValueSlot, sourceCapacity> values{};
semantic_display::PackageView displayPackage{};
semantic_display::DataStore dataStore{values.data(), values.size()};
semantic_display::HistoryStore historyStore{historyMemory.data(), historyMemory.size()};
slstm32::drivers::At24c256 configEeprom{remote_a::eepromHardware()};
semantic_display::ConfigStore configStore{configEeprom, configStorageLayout};
remote_a::DeviceSettingsStore deviceSettingsStore{
    configEeprom,
    static_cast<std::uint32_t>(configStorageLayout.settingsOffset()),
    configStorageLayout.settingsBytes};
semantic_display::ConfigCompiler configCompiler{{
    remote_a::maxStagedConfigBytes,
    compiledConfigCapacity,
    historyCapacity,
    255u,
    16u,
    static_cast<std::uint16_t>(values.size()),
}};

bool formatLocalClock(void*, char* output, std::size_t capacity) {
    if (!output || capacity < 6u) return false;
    RTC_TimeTypeDef time{};
    RTC_DateTypeDef date{};
    if (HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK ||
        HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK) return false;
    return std::snprintf(output, capacity, "%02u:%02u", time.Hours, time.Minutes) > 0;
}

#if REMOTE_A_USE_STLINK_DATA_LINK
remote_a::UartTransport dataTransport{&huart1, USART1_IRQn, signalLed};
#else
remote_a::UartTransport dataTransport{&huart2, USART2_IRQn, signalLed};
#endif
remote_a::FeedbackService feedback{runtime, events, signalLed, statusLed, buzzer};

void modalChanged(void* context, bool visible, semantic_display::ModalSeverity severity) {
    auto& service = *static_cast<remote_a::FeedbackService*>(context);
    service.setAlert(visible && (severity == semantic_display::ModalSeverity::error ||
                                 severity == semantic_display::ModalSeverity::alarm));
}

void modalPresented(void* context, semantic_display::ModalSeverity severity) {
    if (severity != semantic_display::ModalSeverity::error &&
        severity != semantic_display::ModalSeverity::alarm) return;
    static_cast<remote_a::FeedbackService*>(context)->playAttentionCue();
}

bool displayActionAvailable(void*, semantic_display::InputAction action,
                            std::uint16_t, std::uint16_t);
bool executeDisplayAction(void*, semantic_display::InputAction action,
                          std::uint16_t, std::uint16_t);
std::string_view displayActionLabel(void*, semantic_display::InputAction action,
                                    std::uint16_t, std::uint16_t);

semantic_display::DisplayService display{
    runtime, epdPanel, canvas, dataStore, historyStore,
    {nullptr, &formatLocalClock},
    {100u, 333u, fullRefreshAfterPartialUpdates, 60000u},
    {&feedback, &modalChanged, &modalPresented},
    {},
    {nullptr, &displayActionAvailable, &executeDisplayAction, &displayActionLabel}};

bool displayActionAvailable(void*, semantic_display::InputAction action,
                            std::uint16_t, std::uint16_t) {
    return action == semantic_display::InputAction::action2;
}

bool executeDisplayAction(void*, semantic_display::InputAction action,
                          std::uint16_t, std::uint16_t) {
    if (action == semantic_display::InputAction::action2) display.requestFullRefresh();
    return false;
}

std::string_view displayActionLabel(void*, semantic_display::InputAction action,
                                    std::uint16_t, std::uint16_t) {
    if (action == semantic_display::InputAction::action2) return "REFRESH";
    return {};
}

remote_a::DisplayLinkService displayLink{runtime, dataTransport, displayPackage,
                                         dataStore, display};
remote_a::AlarmService alarmService{display, feedback, displayLink};
remote_a::ConfigurationService configurationService{
    runtime, displayLink, display, configCompiler, configStore, displayPackage,
    compiledConfig.data(), compiledConfig.size(),
    downloadedConfig.data(), downloadedConfig.size(),
    &remote_a::factoryDisplayConfiguration};
remote_a::SetupService setupService{
    display, feedback, dataTransport, configurationService, alarmService,
    deviceSettingsStore};

void openSetup(void* context) {
    static_cast<remote_a::SetupService*>(context)->open();
}

remote_a::DisplayInputService displayInput{
    runtime, events, buttons, display, feedback,
    {&setupService, &openSetup}};

void disableInactiveDataUart() {
#if REMOTE_A_USE_STLINK_DATA_LINK
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    (void)HAL_UART_Abort(&huart2);
    __HAL_UART_DISABLE(&huart2);
    __HAL_RCC_USART2_CLK_DISABLE();
#else
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    (void)HAL_UART_Abort(&huart1);
    __HAL_UART_DISABLE(&huart1);
    __HAL_RCC_USART1_CLK_DISABLE();
#endif
}

bool configureDataUart() {
#if REMOTE_A_USE_STLINK_DATA_LINK
    // Remote-a PCB v1 connects STDC14 VCP RX/TX to the same-named target nets.
    // ST names these from the probe's perspective, so PA9/PA10 are physically
    // crossed. The L431 USART swap feature corrects that board wiring.
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_SWAP_INIT;
    huart1.AdvancedInit.Swap = UART_ADVFEATURE_SWAP_ENABLE;
    return HAL_UART_Init(&huart1) == HAL_OK;
#else
    return true;
#endif
}

namespace component {
struct Buttons;
struct SignalLed;
struct StatusLed;
struct Buzzer;
struct DisplayPanel;
struct DataTransport;
struct Setup;
struct Feedback;
struct DisplayRuntime;
struct DisplayInput;
struct Configuration;
struct Alarms;
struct DisplayLink;
} // namespace component

auto application = slstm32::makeApplication(
    slstm32::makeDriverSet(
        slstm32::bind<component::Buttons>(buttons),
        slstm32::bind<component::SignalLed>(signalLed),
        slstm32::bind<component::StatusLed>(statusLed),
        slstm32::bind<component::Buzzer>(buzzer),
        slstm32::bind<component::DisplayPanel>(epdPanel),
        slstm32::bind<component::DataTransport>(dataTransport)),
    slstm32::makeServiceSet(
        slstm32::bind<component::Feedback>(feedback),
        slstm32::bind<component::DisplayRuntime>(display),
        slstm32::bind<component::Alarms>(alarmService),
        slstm32::bind<component::Configuration>(configurationService),
        slstm32::bind<component::Setup>(setupService),
        slstm32::bind<component::DisplayInput>(displayInput),
        slstm32::bind<component::DisplayLink>(displayLink)));

} // namespace

extern "C" void remote_a_app_init(void) {
    semantic_display::StoredConfigInfo stored{};
    const bool eepromDetected = remote_a::eepromReady();
    bool loaded = eepromDetected &&
        configStore.load(compiledConfig.data(), compiledConfig.size(), stored);
    const auto staged = remote_a::stagedConfiguration();
    const char* warningTitle = nullptr;
    const char* warningMessage = nullptr;
    std::size_t packageSize = loaded ? stored.packageSize : 0u;

    if (!eepromDetected) {
        warningTitle = "EEPROM NOT FOUND";
        warningMessage = "AT24C256 DID NOT RESPOND AT I2C ADDRESS 0X52.";
    }

    if (staged.valid && (!loaded || stored.sourceCrc32 != staged.sourceCrc32)) {
        const auto result = configCompiler.compile(staged.json, compiledConfig.data(), compiledConfig.size());
        if (result) {
            packageSize = result.packageSize;
            semantic_display::StoredConfigInfo committed{};
            remote_a::clearEepromIoError();
            if (!configStore.commit(compiledConfig.data(), packageSize, committed)) {
                warningTitle = "STORAGE ERROR";
                warningMessage = remote_a::eepromIoErrorMessage();
            }
        } else {
            warningTitle = "CONFIG ERROR";
            warningMessage = result.message;
            loaded = configStore.load(compiledConfig.data(), compiledConfig.size(), stored);
            packageSize = loaded ? stored.packageSize : 0u;
        }
    }

    if (!staged.valid && useDevelopmentDisplayConfiguration && !loaded) {
        const auto source = remote_a::testDisplayConfiguration();
        const auto result = configCompiler.compile(source, compiledConfig.data(),
                                             compiledConfig.size());
        if (!result) Error_Handler();
        packageSize = result.packageSize;
        semantic_display::StoredConfigInfo committed{};
        remote_a::clearEepromIoError();
        if (!configStore.commit(compiledConfig.data(), packageSize, committed) &&
            showDevelopmentStorageWarnings && !warningTitle) {
            warningTitle = "STORAGE ERROR";
            warningMessage = remote_a::eepromIoErrorMessage();
        }
    }

    if (packageSize == 0u) {
        auto result = configCompiler.compile(staged.valid || !useDevelopmentDisplayConfiguration
                                           ? remote_a::factoryDisplayConfiguration()
                                           : remote_a::testDisplayConfiguration(),
                                       compiledConfig.data(), compiledConfig.size());
        if (!result) {
            result = configCompiler.compile(remote_a::factoryDisplayConfiguration(), compiledConfig.data(),
                                      compiledConfig.size());
        }
        if (!result) Error_Handler();
        packageSize = result.packageSize;
        semantic_display::StoredConfigInfo committed{};
        remote_a::clearEepromIoError();
        if (!configStore.commit(compiledConfig.data(), packageSize, committed) &&
            (!useDevelopmentDisplayConfiguration || showDevelopmentStorageWarnings) &&
            !warningTitle) {
            warningTitle = "STORAGE ERROR";
            warningMessage = remote_a::eepromIoErrorMessage();
        }
    }

    displayPackage = {compiledConfig.data(), packageSize};
    if (!display.setPackage(displayPackage)) Error_Handler();
    setupService.setStorageAvailable(eepromDetected);
    if (!configureDataUart()) Error_Handler();
    disableInactiveDataUart();
    if (!application.init()) Error_Handler();
    if (warningTitle) {
        display.showModal(warningTitle, warningMessage, semantic_display::ModalSeverity::error);
    }
    feedback.setOperational();
}

extern "C" void remote_a_app_run(void) {
    events.process();
    application.run();
    if (!display.sleeping()) {
        if (sleepPeripheralsSuspended) {
            dataTransport.setSleeping(false);
            displayLink.transportRestored();
            sleepPeripheralsSuspended = false;
        }
        buttonInterruptObserved = false;
        return;
    }

    if (!sleepPeripheralsSuspended) {
        dataTransport.setSleeping(true);
        sleepPeripheralsSuspended = true;
    }
    if (!feedback.sleepCueComplete()) return;

    // Stop 2 powers down the high-speed clocks and stops TIM2, so the status
    // LED is intentionally off. Button EXTI lines remain wake sources. Masking
    // interrupts across entry closes the edge-immediately-before-sleep race.
    __disable_irq();
    if (!buttonInterruptObserved) {
        HAL_SuspendTick();
        // Do not let a SysTick that became pending just before suspension cause
        // an immediate, otherwise unexplained Stop 2 wake/re-entry cycle.
        SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
        HAL_DBGMCU_DisableDBGStopMode();
        HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
        SystemClock_Config();
        HAL_ResumeTick();
    }
    __enable_irq();
}

extern "C" void HAL_GPIO_EXTI_Callback(std::uint16_t pin) {
    buttonInterruptObserved = true;
    (void)buttons.onInterrupt(pin);
}
extern "C" void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&huart1); }
extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* uart, std::uint16_t size) {
    dataTransport.onReceive(uart, size);
}
extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef* uart) {
    dataTransport.onError(uart);
}
