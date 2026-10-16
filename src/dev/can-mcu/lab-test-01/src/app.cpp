#include "lab_test_01/app.h"
#include "board.hpp"
#include "can_test_service.hpp"
#include "can_mcu/board.hpp"
#include "can_mcu/can_bus.hpp"
#include "can_mcu/i2c_bus.hpp"
#include "can_mcu/mcu_monitor.hpp"
#include "device_settings.hpp"
#include "diagnostics_service.hpp"
#include "events.hpp"
#include "hardware_test_service.hpp"
#include "i2c_scan_service.hpp"
#include "slstm32/application.hpp"
#include "slstm32/drivers/at24c256.hpp"
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/can_bus.hpp"
#include "slstm32/drivers/led.hpp"
#include "slstm32/drivers/pwm_led.hpp"
#include "slstm32/drivers/serial_log.hpp"
#include "slstm32/drivers/tmp75.hpp"
#include "slstm32/drivers/tone.hpp"
#include "slstm32/event_bus.hpp"
#include <array>
#include <cstdint>
extern "C" {
#include "main.h"
}

namespace {

using slstm32::LogLevel;

constexpr std::size_t logBufferBytes = 1024u;
constexpr std::uint32_t startupLogFlushMs = 200u;
constexpr std::uint8_t eepromAddress = 0x52u;
constexpr std::uint8_t boardTemperatureAddress = 0x4Fu;
constexpr std::size_t canReceiveFrames = 16u;
constexpr std::size_t canTransmitFrames = 8u;

const slstm32::Runtime runtime = can_mcu::runtime();
slstm32::EventBus events{runtime};

std::array<std::uint8_t, logBufferBytes> logBuffer{};
slstm32::drivers::SerialLogDriver serialLog{runtime, can_mcu::vcpOutput(), logBuffer.data(),
                                            logBuffer.size()};
const slstm32::Logger logger = serialLog.logger();

can_mcu::HalI2cBus& i2c = can_mcu::i2cBus();
slstm32::drivers::At24c256I2cBinding eepromBinding{runtime, i2c, eepromAddress};
slstm32::drivers::At24c256 eeprom{eepromBinding.hardware()};
lab_test_01::DeviceSettingsStore settingsStore{eeprom};

const auto buttonConfigs = lab_test_01::board::buttons();
slstm32::drivers::ButtonDriver buttons{runtime, events, lab_test_01::buttonPressed,
                                       buttonConfigs.data(), buttonConfigs.size()};
slstm32::drivers::PwmLedDriver greenLed{runtime, lab_test_01::board::greenLed()};
slstm32::drivers::PwmLedDriver redLed{runtime, lab_test_01::board::redLed()};
slstm32::drivers::ToneDriver buzzer{runtime, lab_test_01::board::buzzer()};
can_mcu::McuMonitor mcu;
slstm32::drivers::Tmp75Driver boardTemperature{runtime, i2c, boardTemperatureAddress};
std::array<slstm32::drivers::CanFrame, canReceiveFrames> canReceiveBuffer{};
std::array<slstm32::drivers::CanFrame, canTransmitFrames> canTransmitBuffer{};
slstm32::drivers::CanBusDriver can{runtime, can_mcu::canBusHardware(), canReceiveBuffer.data(),
                                   canReceiveBuffer.size(), canTransmitBuffer.data(),
                                   canTransmitBuffer.size()};
slstm32::drivers::LedDriver canActivityLed{runtime, can_mcu::canActivityLed()};

lab_test_01::HardwareTestService hardwareTest{
    runtime, events, logger, buttons, static_cast<std::uint8_t>(buttonConfigs.size()),
    greenLed, redLed, buzzer};
lab_test_01::DiagnosticsService diagnostics{runtime, logger, events, mcu, boardTemperature,
                                            i2c, buzzer};
lab_test_01::I2cScanService i2cScan{runtime, i2c, logger};
lab_test_01::CanTestService canTest{runtime, logger, can, canActivityLed};

namespace component {
struct SerialLog;
struct Buttons;
struct GreenLed;
struct RedLed;
struct Buzzer;
struct Mcu;
struct BoardTemperature;
struct Can;
struct CanActivityLed;
struct HardwareTest;
struct Diagnostics;
struct I2cScan;
struct CanTest;
} // namespace component

auto application = slstm32::makeApplication(
    slstm32::makeDriverSet(
        slstm32::bind<component::SerialLog>(serialLog),
        slstm32::bind<component::Buttons>(buttons),
        slstm32::bind<component::GreenLed>(greenLed),
        slstm32::bind<component::RedLed>(redLed),
        slstm32::bind<component::Buzzer>(buzzer),
        slstm32::bind<component::Mcu>(mcu),
        slstm32::bind<component::BoardTemperature>(boardTemperature),
        slstm32::bind<component::Can>(can),
        slstm32::bind<component::CanActivityLed>(canActivityLed)),
    slstm32::makeServiceSet(
        slstm32::bind<component::HardwareTest>(hardwareTest),
        slstm32::bind<component::Diagnostics>(diagnostics),
        slstm32::bind<component::I2cScan>(i2cScan),
        slstm32::bind<component::CanTest>(canTest)));

// Counts boots in EEPROM: proves read, write, and persistence across resets.
void updateBootCount() {
    if (!eepromBinding.present()) {
        logger.writef(LogLevel::error, "eeprom: no answer at 0x%02X", static_cast<unsigned>(eepromAddress));
        return;
    }
    lab_test_01::DeviceSettings settings{};
    const bool loaded = settingsStore.load(settings);
    ++settings.bootCount;
    eepromBinding.clearError();
    if (!settingsStore.save(settings)) {
        logger.writef(LogLevel::error, "eeprom: settings write failed (error %u)",
                      static_cast<unsigned>(eepromBinding.lastError()));
        return;
    }
    logger.writef(LogLevel::info, "eeprom: boot #%lu%s", static_cast<unsigned long>(settings.bootCount),
                  loaded ? "" : " (no previous settings, defaults written)");
}

} // namespace

extern "C" void lab_test_01_app_init(void) {
    can_mcu::init();
    logger.write(LogLevel::info, "");
    logger.writef(LogLevel::info, "lab-test-01 (can-mcu + host-dev), built %s %s", __DATE__,
                  __TIME__);
    can_mcu::logBoardInfo(logger);
    serialLog.flush(startupLogFlushMs);
    updateBootCount();
    serialLog.flush(startupLogFlushMs);
    can_mcu::attachCanDriver(can);
    if (!application.init()) {
        logger.write(LogLevel::error, "application init failed");
        redLed.blink(250u, 125u);
    }
    serialLog.flush(startupLogFlushMs);
}

extern "C" void lab_test_01_app_run(void) {
    events.process();
    application.run();
}

extern "C" void lab_test_01_app_tick(void) {
    // Keep short tone deadlines accurate while the foreground is busy.
    buzzer.onTick();
}

extern "C" void HAL_GPIO_EXTI_Callback(std::uint16_t pin) {
    (void)buttons.onInterrupt(pin);
}
