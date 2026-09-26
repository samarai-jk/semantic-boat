#include "remote_a/app.h"
#include "board.hpp"
#include "events.hpp"
#include "feedback_service.hpp"
#include "hal_epd_transport.hpp"
#include "navigation_service.hpp"
#include "rs485_bridge.hpp"
#include "slstm32/application.hpp"
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/led.hpp"
#include "slstm32/drivers/rgb_led.hpp"
#include "slstm32/drivers/tone.hpp"
#include "slstm32/epd/monochrome_canvas.hpp"
#include "slstm32/epd/waveshare_3in7.hpp"
#include "slstm32/event_bus.hpp"
#include <array>
extern "C" {
#include "main.h"
}

namespace {

std::uint32_t millis() { return HAL_GetTick(); }
volatile std::uint32_t criticalDepth{};
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
// At the expected maximum of three partial updates per second, this schedules
// a full anti-ghosting refresh approximately every 30 minutes. Set to zero to
// disable periodic forced full refreshes entirely.
constexpr remote_a::NavigationService::RefreshPolicy navigationRefreshPolicy{5400u};
remote_a::Rs485Bridge rs485{signalLed};
remote_a::FeedbackService feedback{runtime, events, signalLed, statusLed, buzzer};
remote_a::NavigationService navigation{runtime, events, epdPanel, canvas,
                                       navigationRefreshPolicy};

namespace component {
struct Buttons;
struct SignalLed;
struct StatusLed;
struct Buzzer;
struct Display;
struct Rs485;
struct Feedback;
struct Navigation;
} // namespace component

auto application = slstm32::makeApplication(
    slstm32::makeDriverSet(
        slstm32::bind<component::Buttons>(buttons),
        slstm32::bind<component::SignalLed>(signalLed),
        slstm32::bind<component::StatusLed>(statusLed),
        slstm32::bind<component::Buzzer>(buzzer),
        slstm32::bind<component::Display>(epdPanel),
        slstm32::bind<component::Rs485>(rs485)),
    slstm32::makeServiceSet(
        slstm32::bind<component::Feedback>(feedback),
        slstm32::bind<component::Navigation>(navigation)));

} // namespace

extern "C" void remote_a_app_init(void) {
    if (!application.init()) Error_Handler();
}

extern "C" void remote_a_app_run(void) {
    events.process();
    application.run();
}

extern "C" void HAL_GPIO_EXTI_Callback(std::uint16_t pin) { (void)buttons.onInterrupt(pin); }
extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* uart, std::uint16_t size) { rs485.onReceive(uart, size); }
extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef* uart) { rs485.onError(uart); }
