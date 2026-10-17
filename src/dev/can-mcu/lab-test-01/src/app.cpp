#include "lab_test_01/app.h"
#include "board.hpp"
#include "can_mcu/board.hpp"
#include "can_mcu/can_bus.hpp"
#include "can_mcu/i2c_bus.hpp"
#include "can_mcu/mcu_monitor.hpp"
#include "can_mcu/supervision.hpp"
#include "can_monitor_service.hpp"
#include "device_settings.hpp"
#include "dev_tools.hpp"
#include "diagnostics_service.hpp"
#include "events.hpp"
#include "health_service.hpp"
#include "i2c_scan_service.hpp"
#include "n2k/name.hpp"
#include "n2k/node.hpp"
#include "n2k/pgn.hpp"
#include "semantic_modules/ad_input_iso_12v.hpp"
#include "semantic_modules/ad_input_iso_12v_calibration.hpp"
#include "slstm32/console.hpp"
#include "slstm32/application.hpp"
#include "slstm32/build_profile.hpp"
#include "slstm32/drivers/at24c256.hpp"
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/can_bus.hpp"
#include "slstm32/drivers/led.hpp"
#include "slstm32/drivers/pwm_led.hpp"
#include "slstm32/drivers/serial_log.hpp"
#include "slstm32/drivers/tmp75.hpp"
#include "slstm32/drivers/tone.hpp"
#include "slstm32/event_bus.hpp"
#include "temperature_report_service.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
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
constexpr std::uint32_t watchdogTimeoutMs = 4000u;

// NMEA 2000 identity. LT1 is a lab device and uses the development
// manufacturer code; product firmware must use the code assigned by NMEA.
constexpr std::uint16_t n2kManufacturerCode = n2k::developmentManufacturerCode;
static_assert(!slstm32::production || n2kManufacturerCode != n2k::developmentManufacturerCode,
              "Product firmware needs the NMEA 2000 manufacturer code assigned by NMEA (AGENTS.md)");
constexpr std::uint8_t n2kDeviceClassSensors = 75u;     // Sensor Communication Interface
constexpr std::uint8_t n2kDeviceFunctionTemperature = 130u;

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
lab_test_01::DeviceSettings settings{};
bool settingsAvailable{};

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

// 12v-ad-input-iso module on the I2C connectors (ADS1115 at 0x49, R34 fitted),
// DI0..DI3 wired to PA4..PA7.
semantic_modules::AdInputIso12vConfig inputModuleConfig{
    0x49u,
    slstm32::drivers::Ads1115Driver::Gain::fsr2048,
    slstm32::drivers::Ads1115Driver::DataRate::sps128,
    lab_test_01::board::isolatedModuleInputs(),
    20u,
};
semantic_modules::AdInputIso12v inputModule{runtime, i2c, inputModuleConfig};
semantic_modules::AdInputIso12v* const inputModules[]{&inputModule};
semantic_modules::AdInputIso12vCalibrationStore inputModuleCalibrationStore{eeprom,
                                                                            lab_test_01::moduleCalibrationOffset};

// Filled in at startup from the MCU unique ID: NAME unique number and the
// serial code of the product information.
char serialCode[25]{};
constexpr std::uint32_t n2kTransmitPgns[]{n2k::pgn::temperatureExtendedRange, n2k::pgn::batteryStatus,
                                           n2k::pgn::binarySwitchBankStatus};
n2k::NodeConfig n2kConfig{
    n2k::Name{0u, n2kManufacturerCode, 0u, 0u, n2kDeviceFunctionTemperature,
              n2kDeviceClassSensors, 0u, 4u, true},
    lab_test_01::DeviceSettings{}.n2kAddress,
    // Builds with development tools say so in the software version, visible
    // to every NMEA 2000 tool (Signal K, can-bench).
    n2k::ProductInfo{2100u, 1u, "Semantic Boat LT1",
                     slstm32::devTools ? "0.1.0-dev " __DATE__ : "0.1.0 " __DATE__,
                     "can-mcu V1.0 + host-dev", serialCode, 0u, 1u},
    n2kTransmitPgns,
    sizeof n2kTransmitPgns / sizeof n2kTransmitPgns[0],
    nullptr,
    0u,
    60000u,
};
n2k::Node n2kNode{runtime, can, n2kConfig, logger};

lab_test_01::DiagnosticsService diagnostics{runtime, logger, events, mcu, boardTemperature,
                                            i2c, buzzer};
lab_test_01::I2cScanService i2cScan{runtime, i2c, logger};
lab_test_01::CanMonitorService canMonitor{runtime, logger, can, n2kNode, canActivityLed};
lab_test_01::TemperatureReportService temperatureReport{runtime, n2kNode, boardTemperature, mcu};
semantic_modules::AdInputIso12vCalibrator inputModuleCalibrator{
    runtime, logger, inputModuleCalibrationStore, inputModules, sizeof inputModules / sizeof inputModules[0]};
slstm32::CommandConsole console{can_mcu::vcpInput(), logger};
lab_test_01::HealthService health{runtime, logger, eeprom, lab_test_01::healthRecordOffset, mcu};

// Development and test tools (src/dev/), or a no-op without them; see dev_tools.hpp.
const lab_test_01::DevToolsContext devToolsContext{
    runtime, events, logger, serialLog, console, buttons, static_cast<std::uint8_t>(buttonConfigs.size()),
    greenLed, redLed, buzzer, i2c, i2cScan, inputModule, n2kNode, health};

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
struct InputModule;
struct Diagnostics;
struct I2cScan;
struct N2kNode;
struct CanMonitor;
struct TemperatureReport;
struct InputModuleCalibrator;
struct Console;
struct Health;
struct DevTools;
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
        slstm32::bind<component::CanActivityLed>(canActivityLed),
        slstm32::bind<component::InputModule>(inputModule)),
    slstm32::makeServiceSet(
        slstm32::bind<component::Health>(health),
        slstm32::bind<component::Diagnostics>(diagnostics),
        slstm32::bind<component::I2cScan>(i2cScan),
        slstm32::bind<component::N2kNode>(n2kNode),
        slstm32::bind<component::CanMonitor>(canMonitor),
        slstm32::bind<component::TemperatureReport>(temperatureReport),
        slstm32::bind<component::InputModuleCalibrator>(inputModuleCalibrator),
        slstm32::bind<component::Console>(console),
        slstm32::bind<component::DevTools>(lab_test_01::devTools(devToolsContext))));

bool saveSettings() {
    eepromBinding.clearError();
    if (settingsStore.save(settings)) return true;
    logger.writef(LogLevel::error, "eeprom: settings write failed (error %u)",
                  static_cast<unsigned>(eepromBinding.lastError()));
    return false;
}

// Counts boots in EEPROM: proves read, write, and persistence across resets.
void loadSettings() {
    if (!eepromBinding.present()) {
        logger.writef(LogLevel::error, "eeprom: no answer at 0x%02X", static_cast<unsigned>(eepromAddress));
        return;
    }
    const bool loaded = settingsStore.load(settings);
    ++settings.bootCount;
    settingsAvailable = saveSettings();
    if (!settingsAvailable) return;
    logger.writef(LogLevel::info, "eeprom: boot #%lu%s", static_cast<unsigned long>(settings.bootCount),
                  loaded ? "" : " (no previous settings, defaults written)");
}

void configureN2kIdentity() {
    std::uint8_t uid[12];
    can_mcu::uniqueId(uid);
    n2kConfig.name.uniqueNumber = n2k::uniqueNumberFrom(uid, sizeof uid);
    for (std::size_t index = 0u; index < sizeof uid; ++index) {
        std::snprintf(serialCode + 2u * index, 3u, "%02X", static_cast<unsigned>(uid[sizeof uid - 1u - index]));
    }
    n2kConfig.preferredAddress = settings.n2kAddress;
    logger.writef(LogLevel::info, "n2k: unique number %lu, preferred address %u",
                  static_cast<unsigned long>(n2kConfig.name.uniqueNumber),
                  static_cast<unsigned>(n2kConfig.preferredAddress));
}

// Bench feedback for the automatic input module calibration: red LED on while
// a channel is measured, green flash and a quiet click when it is done, fast
// red blinking when the contact was lost or the result was rejected.
void onCalibrationEvent(void*, std::size_t, semantic_modules::AdInputIso12vCalibrator::AutoEvent event) {
    using Event = semantic_modules::AdInputIso12vCalibrator::AutoEvent;
    constexpr std::uint32_t clickHz = 2700u;
    constexpr std::uint32_t clickMs = 60u;
    constexpr float clickLevel = 0.02f;
    switch (event) {
    case Event::detected:
        redLed.on();
        break;
    case Event::lost:
    case Event::rejected:
        redLed.blink(150u, 75u);
        break;
    case Event::done:
        redLed.off();
        greenLed.pulse(800u);
        buzzer.play(clickHz, clickMs, clickLevel);
        break;
    case Event::finished:
        redLed.off();
        greenLed.blink(2000u, 60u);
        break;
    }
}

void onN2kAddressChange(void*, std::uint8_t address) {
    if (!settingsAvailable || settings.n2kAddress == address) return;
    settings.n2kAddress = address;
    if (saveSettings()) {
        logger.writef(LogLevel::info, "n2k: address %u stored", static_cast<unsigned>(address));
    }
}

} // namespace

extern "C" void lab_test_01_app_init(void) {
    can_mcu::init();
    can_mcu::startPowerMonitor(can_mcu::PowerThreshold::v2_9);
    logger.write(LogLevel::info, "");
    logger.writef(LogLevel::info, "lab-test-01 (can-mcu + host-dev), built %s %s", __DATE__,
                  __TIME__);
    if constexpr (slstm32::devTools) {
        logger.write(LogLevel::warning, "DEV BUILD: development and test tools compiled in, not for production");
    }
    can_mcu::logBoardInfo(logger);
    serialLog.flush(startupLogFlushMs);
    loadSettings();
    configureN2kIdentity();
    serialLog.flush(startupLogFlushMs);
    can_mcu::attachCanDriver(can);
    n2kNode.onMessage(&lab_test_01::CanMonitorService::onMessage, &canMonitor);
    n2kNode.onAddressChange(&onN2kAddressChange, nullptr);
    inputModuleCalibrator.onAutoEvent(&onCalibrationEvent, nullptr);
    if (!inputModuleCalibrator.registerCommands(console) || !diagnostics.registerCommands(console) ||
        !health.registerCommands(console)) {
        logger.write(LogLevel::error, "console: commands not registered");
    }
    if (!application.init()) {
        logger.write(LogLevel::error, "application init failed");
        redLed.blink(250u, 125u);
    }
    if (boardTemperature.defaultConfiguration()) {
        logger.write(LogLevel::info, "tmp75: 12-bit setting not accepted, using the default configuration");
    }
    serialLog.flush(startupLogFlushMs);
    // A hang from here on becomes a logged watchdog restart.
    can_mcu::startWatchdog(watchdogTimeoutMs);
    logger.writef(LogLevel::info, "supervision: watchdog %lu ms, power monitor below 2.9 V",
                  static_cast<unsigned long>(watchdogTimeoutMs));
}

extern "C" void lab_test_01_app_run(void) {
    can_mcu::refreshWatchdog();
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
