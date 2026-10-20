// engine-mon composition root: a marine diesel engine monitor on the can-mcu
// (on the bench with host-dev). This first version brings up the board, the
// host inputs and outputs, B1S with configuration over CAN, and the health
// record; the engine's own channels (rpm, temperatures, pressures, alarms)
// follow once the sensors are chosen. See ../README.md.
#include "engine_mon/app.h"
#include "board.hpp"
#include "can_mcu/board.hpp"
#include "can_mcu/board_temperatures.hpp"
#include "can_mcu/can_bus.hpp"
#include "can_mcu/can_monitor_service.hpp"
#include "can_mcu/firmware.hpp"
#include "can_mcu/health_service.hpp"
#include "can_mcu/i2c_bus.hpp"
#include "can_mcu/i2c_scan_service.hpp"
#include "can_mcu/image_header.hpp"
#include "can_mcu/mcu_monitor.hpp"
#include "can_mcu/supervision.hpp"
#include "device_settings.hpp"
#include "dev_tools.hpp"
#include "events.hpp"
#include "io_console.hpp"
#include "b1s/measurement_publisher.hpp"
#include "b1s/registry.hpp"
#include "b1s/service_server.hpp"
#include "n2k/name.hpp"
#include "n2k/node.hpp"
#include "n2k/pgn.hpp"
#include "n2k/temperature.hpp"
#include "slstm32/application.hpp"
#include "slstm32/build_profile.hpp"
#include "slstm32/console.hpp"
#include "slstm32/drivers/at24c256.hpp"
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/can_bus.hpp"
#include "slstm32/drivers/digital_input.hpp"
#include "slstm32/drivers/led.hpp"
#include "slstm32/drivers/pwm_output.hpp"
#include "slstm32/drivers/serial_log.hpp"
#include "slstm32/drivers/tmp75.hpp"
#include "slstm32/event_bus.hpp"
#include "slstm32/sensors/measurement.hpp"
#include "slstm32/sensors/measurement_console.hpp"
#include "slstm32/sensors/measurement_log.hpp"
#include "slstm32/sensors/registry.hpp"
#include "slstm32/storage/keyed_record_store.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
extern "C" {
#include "main.h"
}

namespace {

using slstm32::LogLevel;
namespace sensors = slstm32::sensors;
using sensors::Comparison;
using sensors::Severity;
using sensors::ValueFunction;

constexpr std::size_t logBufferBytes = 1024u;
constexpr std::uint32_t startupLogFlushMs = 200u;
constexpr std::uint8_t eepromAddress = 0x52u;
constexpr std::uint8_t boardTemperatureAddress = 0x4Fu;
constexpr std::size_t canReceiveFrames = 16u;
constexpr std::size_t canTransmitFrames = 8u;
constexpr std::uint32_t watchdogTimeoutMs = 4000u;
constexpr std::uint32_t inputDebounceMs = 20u;

// B1S / NMEA 2000 identity from the B1S registry (product code, class 50
// "propulsion", function 140 "engine room monitoring").
constexpr b1s::Product product = b1s::product::engineMon;
// Builds with development tools say so in the software version, visible to
// every NMEA 2000 tool (Signal K, can-bench) and in the image header.
#define ENGINE_MON_VERSION "0.1.0"
constexpr const char* softwareVersion =
    slstm32::devTools ? ENGINE_MON_VERSION "-dev " __DATE__ : ENGINE_MON_VERSION " " __DATE__;
// Firmware image header for the bootloader; size and CRC are stamped in
// after the link (src/lib/b1s/FIRMWARE.md).
[[gnu::section(".image_header"), gnu::used]] const can_mcu::image::Header imageHeader =
    can_mcu::image::makeHeader(product.code, 0u, 1u, 0u, slstm32::devTools,
                               slstm32::devTools ? ENGINE_MON_VERSION "-dev " __DATE__ : ENGINE_MON_VERSION " " __DATE__);
// The application confirms a newly installed image once it has run normally this long.
constexpr std::uint32_t firmwareConfirmAfterMs = 10000u;

const slstm32::Runtime runtime = can_mcu::runtime();
slstm32::EventBus events{runtime};

std::array<std::uint8_t, logBufferBytes> logBuffer{};
slstm32::drivers::SerialLogDriver serialLog{runtime, can_mcu::vcpOutput(), logBuffer.data(),
                                            logBuffer.size()};
const slstm32::Logger logger = serialLog.logger();

can_mcu::HalI2cBus& i2c = can_mcu::i2cBus();
slstm32::drivers::At24c256I2cBinding eepromBinding{runtime, i2c, eepromAddress};
slstm32::drivers::At24c256 eeprom{eepromBinding.hardware()};
engine_mon::DeviceSettingsStore settingsStore{eeprom};
engine_mon::DeviceSettings settings{};
bool settingsAvailable{};

// --- Host inputs and outputs (board.hpp) ----------------------------------------

const auto buttonConfigs = engine_mon::board::buttons();
slstm32::drivers::ButtonDriver buttons{runtime, events, engine_mon::buttonPressed,
                                       buttonConfigs.data(), buttonConfigs.size()};

const auto inputHardware = engine_mon::board::digitalInputs();
slstm32::drivers::DigitalInputDriver di0{runtime, inputHardware[0], inputDebounceMs};
slstm32::drivers::DigitalInputDriver di1{runtime, inputHardware[1], inputDebounceMs};
slstm32::drivers::DigitalInputDriver di2{runtime, inputHardware[2], inputDebounceMs};
slstm32::drivers::DigitalInputDriver di3{runtime, inputHardware[3], inputDebounceMs};
slstm32::drivers::DigitalInputDriver di4{runtime, inputHardware[4], inputDebounceMs};
slstm32::drivers::DigitalInputDriver di5{runtime, inputHardware[5], inputDebounceMs};
slstm32::drivers::DigitalInputDriver di6{runtime, inputHardware[6], inputDebounceMs};
slstm32::drivers::DigitalInputDriver di7{runtime, inputHardware[7], inputDebounceMs};
slstm32::drivers::DigitalInputDriver* const digitalInputs[]{&di0, &di1, &di2, &di3, &di4, &di5, &di6, &di7};

const auto outputHardware = engine_mon::board::outputs();
slstm32::drivers::PwmOutputDriver do0{outputHardware[0]};
slstm32::drivers::PwmOutputDriver do1{outputHardware[1]};
slstm32::drivers::PwmOutputDriver do2{outputHardware[2]};
slstm32::drivers::PwmOutputDriver* const outputs[]{&do0, &do1, &do2};

can_mcu::McuMonitor mcu;
// McuMonitor input indices of AI0..AI2 (added in init).
std::size_t analogInputs[engine_mon::board::analogInputCount]{};

slstm32::drivers::Tmp75Driver boardTemperature{runtime, i2c, boardTemperatureAddress};
std::array<slstm32::drivers::CanFrame, canReceiveFrames> canReceiveBuffer{};
std::array<slstm32::drivers::CanFrame, canTransmitFrames> canTransmitBuffer{};
slstm32::drivers::CanBusDriver can{runtime, can_mcu::canBusHardware(), canReceiveBuffer.data(),
                                   canReceiveBuffer.size(), canTransmitBuffer.data(),
                                   canTransmitBuffer.size()};
slstm32::drivers::LedDriver canActivityLed{runtime, can_mcu::canActivityLed()};

// Filled in at startup from the MCU unique ID: NAME unique number and the
// serial code of the product information.
char serialCode[25]{};
constexpr std::uint32_t n2kTransmitPgns[]{n2k::pgn::temperatureExtendedRange,
                                           b1s::pgn::measurement,
                                           b1s::pgn::measurementRaw,
                                           b1s::pgn::channelDescription,
                                           b1s::pgn::service,
                                           b1s::pgn::command};
constexpr std::uint32_t n2kReceivePgns[]{b1s::pgn::service, b1s::pgn::command};
// Fast packets engine-mon receives: B1S service requests (configuration over CAN).
constexpr std::uint32_t n2kFastReceivePgns[]{b1s::pgn::service};
std::uint8_t n2kFastReceiveBuffer[96]{};
n2k::NodeConfig n2kConfig{
    n2k::Name{0u, n2k::manufacturerCode, 0u, 0u, product.deviceFunction, product.deviceClass, 0u, 4u, true},
    engine_mon::DeviceSettings{}.n2kAddress,
    n2k::ProductInfo{2100u, product.code, "Semantic Boat Engine Monitor", softwareVersion,
                     "can-mcu V1.0 + host-dev", serialCode, 0u, 1u},
    n2kTransmitPgns,
    sizeof n2kTransmitPgns / sizeof n2kTransmitPgns[0],
    n2kReceivePgns,
    sizeof n2kReceivePgns / sizeof n2kReceivePgns[0],
    60000u,
    n2kFastReceivePgns,
    sizeof n2kFastReceivePgns / sizeof n2kFastReceivePgns[0],
    n2kFastReceiveBuffer,
    sizeof n2kFastReceiveBuffer,
};
n2k::Node n2kNode{runtime, can, n2kConfig, logger};

can_mcu::I2cScanService i2cScan{runtime, i2c, logger};
can_mcu::CanMonitorService canMonitor{runtime, logger, can, n2kNode, canActivityLed};

// Measurement channels: so far the can-mcu's own probes. The enclosure sits
// in the engine room, so its limits are higher than LT1's. Fields: function,
// comparison, severity, latching, window, delay (s), threshold, upper
// (bands), hysteresis; m°C. Rules set over CAN replace these defaults.
constexpr sensors::AlarmRule boardAlarms[]{
    {ValueFunction::average, Comparison::above, Severity::warning, false, {60u, false}, 10u, 65000, 0, 3000},
    {ValueFunction::average, Comparison::above, Severity::alarm, false, {60u, false}, 10u, 75000, 0, 3000},
};
constexpr sensors::AlarmRule mcuAlarms[]{
    {ValueFunction::average, Comparison::above, Severity::alarm, false, {30u, false}, 5u, 80000, 0, 3000},
};
constexpr sensors::MeasurementConfig boardChannelConfig{
    "board", sensors::Quantity::temperature, 0u, 10000u, 4000u, boardAlarms, sizeof boardAlarms / sizeof boardAlarms[0],
    {true, Severity::warning, 10u, false}};
constexpr sensors::MeasurementConfig mcuChannelConfig{
    "mcu", sensors::Quantity::temperature, 1u, 10000u, 8000u, mcuAlarms, sizeof mcuAlarms / sizeof mcuAlarms[0]};
sensors::Measurement boardChannel{boardChannelConfig};
sensors::Measurement mcuChannel{mcuChannelConfig};
sensors::Measurement* const channels[]{&boardChannel, &mcuChannel};

can_mcu::BoardTemperatures boardTemperatures{runtime, boardTemperature, mcu, boardChannel, mcuChannel};
slstm32::storage::KeyedRecordStore::Key sensorSettingsKeys[engine_mon::sensorSettingsSlots]{};
slstm32::storage::KeyedRecordStore sensorSettings{eeprom, engine_mon::sensorSettingsOffset, sensorSettingsKeys};
// Windows for averages, minima, and maxima: an engine monitor has little
// else to do with its RAM, so many fine ones (24 windows of 32 buckets,
// about 5.8 KiB).
sensors::StaticWindowPool<24u, 32u> windows;
static_assert(sizeof channels / sizeof channels[0] <= engine_mon::measurementChannels, "EEPROM area for the channels");
sensors::MeasurementRegistry measurements{runtime, logger, channels, sizeof channels / sizeof channels[0],
                                          &sensorSettings, &events, engine_mon::alarmChanged, &windows,
                                          &eeprom, engine_mon::channelProcessingOffset};
sensors::MeasurementConsole measurementConsole{measurements, logger};
sensors::MeasurementLog measurementLog{runtime, logger, measurements, 10000u};
// The channels on B1S (src/lib/b1s/README.md), sent as B1S messages unless
// configured otherwise; as standard PGN their source is "engine room".
constexpr auto engineRoom = static_cast<std::uint8_t>(n2k::TemperatureSource::engineRoom);
const b1s::ChannelConfig b1sChannels[]{
    {&boardChannel, 0u, b1s::Publish::b1s, engineRoom, "", "Engine monitor enclosure"},
    {&mcuChannel, 1u, b1s::Publish::b1s, engineRoom, "", "Engine monitor MCU die"},
};
b1s::ChannelSettings::Override b1sOverrides[engine_mon::b1sChannelSlots]{};
b1s::ChannelSettings b1sSettings{eeprom, engine_mon::b1sSettingsOffset, b1sOverrides};
b1s::ChannelState b1sStates[sizeof b1sChannels / sizeof b1sChannels[0]]{};
b1s::MeasurementPublisher measurementPublisher{runtime, n2kNode, b1sChannels, b1sStates, 2000u, logger, &b1sSettings};

slstm32::CommandConsole console{can_mcu::vcpInput(), logger};
can_mcu::HealthService health{runtime, logger, eeprom, engine_mon::healthRecordOffset, mcu};
engine_mon::IoConsole ioConsole{logger, digitalInputs, outputs, mcu, analogInputs};

// Configuration, calibration, and the console over CAN (src/lib/b1s/CONFIGURATION.md).
bool readPreferredAddress(void*, std::int32_t& value);
b1s::ParameterStatus writePreferredAddress(void*, std::int32_t value);
b1s::CommandResult runDeviceCommand(void*, b1s::Command command, std::uint8_t channel, std::int32_t argument);
const b1s::DeviceParameter deviceParameters[]{
    {b1s::Parameter::preferredAddress, &readPreferredAddress, &writePreferredAddress, nullptr},
};
const b1s::ServiceServerConfig serviceServerConfig{
    &measurementPublisher, &measurements, &console, &serialLog,
    deviceParameters, sizeof deviceParameters / sizeof deviceParameters[0], &runDeviceCommand, nullptr};
b1s::ServiceServer serviceServer{runtime, n2kNode, serviceServerConfig};
std::uint32_t restartAt{};
bool restartRequested{};
bool bootloaderRequested{};
bool firmwareConfirmed{};

// Development and test tools (src/dev/), or a no-op without them; see dev_tools.hpp.
const engine_mon::DevToolsContext devToolsContext{runtime, logger, console, outputs,
                                                  sizeof outputs / sizeof outputs[0]};

namespace component {
struct SerialLog;
struct Buttons;
struct Di0;
struct Di1;
struct Di2;
struct Di3;
struct Di4;
struct Di5;
struct Di6;
struct Di7;
struct Do0;
struct Do1;
struct Do2;
struct Mcu;
struct BoardTemperature;
struct Can;
struct CanActivityLed;
struct Health;
struct I2cScan;
struct N2kNode;
struct CanMonitor;
struct BoardTemperatures;
struct Measurements;
struct MeasurementLog;
struct MeasurementPublisher;
struct ServiceServer;
struct Console;
struct DevTools;
} // namespace component

auto application = slstm32::makeApplication(
    slstm32::makeDriverSet(
        slstm32::bind<component::SerialLog>(serialLog),
        slstm32::bind<component::Buttons>(buttons),
        slstm32::bind<component::Di0>(di0),
        slstm32::bind<component::Di1>(di1),
        slstm32::bind<component::Di2>(di2),
        slstm32::bind<component::Di3>(di3),
        slstm32::bind<component::Di4>(di4),
        slstm32::bind<component::Di5>(di5),
        slstm32::bind<component::Di6>(di6),
        slstm32::bind<component::Di7>(di7),
        slstm32::bind<component::Do0>(do0),
        slstm32::bind<component::Do1>(do1),
        slstm32::bind<component::Do2>(do2),
        slstm32::bind<component::Mcu>(mcu),
        slstm32::bind<component::BoardTemperature>(boardTemperature),
        slstm32::bind<component::Can>(can),
        slstm32::bind<component::CanActivityLed>(canActivityLed)),
    slstm32::makeServiceSet(
        slstm32::bind<component::Health>(health),
        slstm32::bind<component::I2cScan>(i2cScan),
        slstm32::bind<component::N2kNode>(n2kNode),
        slstm32::bind<component::CanMonitor>(canMonitor),
        slstm32::bind<component::BoardTemperatures>(boardTemperatures),
        slstm32::bind<component::Measurements>(measurements),
        slstm32::bind<component::MeasurementLog>(measurementLog),
        slstm32::bind<component::MeasurementPublisher>(measurementPublisher),
        slstm32::bind<component::ServiceServer>(serviceServer),
        slstm32::bind<component::Console>(console),
        slstm32::bind<component::DevTools>(engine_mon::devTools(devToolsContext))));

bool saveSettings() {
    eepromBinding.clearError();
    if (settingsStore.save(settings)) return true;
    logger.writef(LogLevel::error, "eeprom: settings write failed (error %u)",
                  static_cast<unsigned>(eepromBinding.lastError()));
    return false;
}

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

// A button press acknowledges the measurement alarms.
void onButton(slstm32::EventId, const void* payload, std::uint8_t size, void*) {
    if (size != sizeof(slstm32::drivers::ButtonEvent)) return;
    const auto& event = *static_cast<const slstm32::drivers::ButtonEvent*>(payload);
    if (event.pressed && measurements.summary().unacknowledged) measurements.acknowledgeAll();
}

// B1S service and command messages go to the service server; everything else
// to the CAN monitor's message log.
void onN2kMessage(void*, const n2k::CanId& id, const std::uint8_t* data, std::uint8_t length) {
    if (serviceServer.handleMessage(id, data, length)) return;
    can_mcu::CanMonitorService::onMessage(&canMonitor, id, data, length);
}

bool readPreferredAddress(void*, std::int32_t& value) {
    value = settings.n2kAddress;
    return true;
}

b1s::ParameterStatus writePreferredAddress(void*, std::int32_t value) {
    if (value < 0 || value > n2k::maximumAddress) return b1s::ParameterStatus::invalidValue;
    if (!settingsAvailable) return b1s::ParameterStatus::busy;
    settings.n2kAddress = static_cast<std::uint8_t>(value);
    return saveSettings() ? b1s::ParameterStatus::done : b1s::ParameterStatus::busy;
}

// Device commands over CAN: identify (the yellow CAN LED blinks fast),
// restart, and restart into the bootloader (both after the reply has gone out).
b1s::CommandResult runDeviceCommand(void*, b1s::Command command, std::uint8_t, std::int32_t argument) {
    switch (command) {
    case b1s::Command::identify:
        canMonitor.identify(static_cast<std::uint32_t>(argument & 0xFFFF) * 1000u);
        return b1s::CommandResult::done;
    case b1s::Command::restart:
    case b1s::Command::enterBootloader:
        bootloaderRequested = command == b1s::Command::enterBootloader;
        logger.write(LogLevel::warning, bootloaderRequested ? "b1s: firmware update requested over CAN, restarting into the bootloader"
                                                            : "b1s: restart requested over CAN");
        restartRequested = true;
        restartAt = runtime.millis() + 300u;
        return b1s::CommandResult::done;
    default:
        return b1s::CommandResult::unknownCommand;
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

extern "C" void engine_mon_app_init(void) {
    can_mcu::init();
    can_mcu::startPowerMonitor(can_mcu::PowerThreshold::v2_9);
    logger.write(LogLevel::info, "");
    logger.writef(LogLevel::info, "engine-mon (can-mcu + host-dev), built %s %s", __DATE__, __TIME__);
    if constexpr (slstm32::devTools) {
        logger.write(LogLevel::warning, "DEV BUILD: development and test tools compiled in, not for production");
    }
    can_mcu::logBoardInfo(logger);
    serialLog.flush(startupLogFlushMs);
    loadSettings();
    configureN2kIdentity();
    serialLog.flush(startupLogFlushMs);
    for (std::size_t index = 0u; index < engine_mon::board::analogInputCount; ++index) {
        analogInputs[index] = mcu.addInput(engine_mon::board::analogChannels[index]);
    }
    can_mcu::attachCanDriver(can);
    n2kNode.onMessage(&onN2kMessage, nullptr);
    n2kNode.onAddressChange(&onN2kAddressChange, nullptr);
    n2kNode.onRequest(&b1s::MeasurementPublisher::handleRequest, &measurementPublisher);
    if (!events.subscribe(engine_mon::buttonPressed, &onButton, nullptr)) {
        logger.write(LogLevel::error, "events: alarm acknowledge button not subscribed");
    }
    if (!health.registerCommands(console) || !measurementConsole.registerCommands(console) ||
        !ioConsole.registerCommands(console)) {
        logger.write(LogLevel::error, "console: commands not registered");
    }
    if (!application.init()) {
        logger.write(LogLevel::error, "application init failed");
        canActivityLed.blink(250u, 125u);
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

extern "C" void engine_mon_app_run(void) {
    can_mcu::refreshWatchdog();
    events.process();
    application.run();
    const auto now = runtime.millis();
    if (!firmwareConfirmed && now >= firmwareConfirmAfterMs) {
        firmwareConfirmed = true;
        can_mcu::confirmFirmware();
    }
    if (restartRequested && static_cast<std::int32_t>(now - restartAt) >= 0) {
        serialLog.flush(startupLogFlushMs);
        if (bootloaderRequested) can_mcu::restartIntoBootloader();
        NVIC_SystemReset();
    }
}

extern "C" void engine_mon_app_tick(void) {}

extern "C" void HAL_GPIO_EXTI_Callback(std::uint16_t pin) {
    (void)buttons.onInterrupt(pin);
}
