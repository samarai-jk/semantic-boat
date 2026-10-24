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
#include "can_mcu/one_wire_pin.hpp"
#include "can_mcu/rtc_clock.hpp"
#include "can_mcu/supervision.hpp"
#include "device_settings.hpp"
#include "dev_tools.hpp"
#include "events.hpp"
#include "io_console.hpp"
#include "b1s/measurement_publisher.hpp"
#include "b1s/registry.hpp"
#include "b1s/service_server.hpp"
#include "n2k/name.hpp"
#include "semantic_modules/ad_input_iso_12v.hpp"
#include "semantic_modules/ad_input_iso_12v_calibration.hpp"
#include "n2k/node.hpp"
#include "n2k/pgn.hpp"
#include "n2k/temperature.hpp"
#include "slstm32/application.hpp"
#include "slstm32/build_profile.hpp"
#include "slstm32/console.hpp"
#include "slstm32/drivers/analog_sensor.hpp"
#include "slstm32/drivers/at24c256.hpp"
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/can_bus.hpp"
#include "slstm32/drivers/ct1780.hpp"
#include "slstm32/drivers/one_wire.hpp"
#include "slstm32/drivers/digital_input.hpp"
#include "slstm32/drivers/led.hpp"
#include "slstm32/drivers/pwm_led.hpp"
#include "slstm32/drivers/pwm_output.hpp"
#include "slstm32/drivers/serial_log.hpp"
#include "slstm32/drivers/tone.hpp"
#include "slstm32/drivers/tone_pattern.hpp"
#include "slstm32/drivers/tmp75.hpp"
#include "slstm32/event_bus.hpp"
#include "slstm32/sensors/alarm_annunciator.hpp"
#include "slstm32/sensors/measurement.hpp"
#include "slstm32/sensors/measurement_console.hpp"
#include "slstm32/sensors/measurement_log.hpp"
#include "slstm32/sensors/registry.hpp"
#include "slstm32/sensors/rtd.hpp"
#include "slstm32/storage/keyed_record_store.hpp"
#include <array>
#include <climits>
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

// The two 12v-ad-input-iso modules: AD1 (0x49) with D10..D13 and A10..A13,
// AD2 (0x48) with D20..D23 and A20..A23. Analog inputs 0..18.8 V at the
// terminal (fsr2048), digital inputs debounced 20 ms.
const auto inputHardware = engine_mon::board::digitalInputs();
const semantic_modules::AdInputIso12vConfig ad1Config{
    engine_mon::board::ad1Address, slstm32::drivers::Ads1115Driver::Gain::fsr2048,
    slstm32::drivers::Ads1115Driver::DataRate::sps128,
    {inputHardware[0], inputHardware[1], inputHardware[2], inputHardware[3]}, inputDebounceMs};
const semantic_modules::AdInputIso12vConfig ad2Config{
    engine_mon::board::ad2Address, slstm32::drivers::Ads1115Driver::Gain::fsr2048,
    slstm32::drivers::Ads1115Driver::DataRate::sps128,
    {inputHardware[4], inputHardware[5], inputHardware[6], inputHardware[7]}, inputDebounceMs};
semantic_modules::AdInputIso12v ad1{runtime, i2c, ad1Config};
semantic_modules::AdInputIso12v ad2{runtime, i2c, ad2Config};
semantic_modules::AdInputIso12v* const inputModules[]{&ad1, &ad2};
semantic_modules::AdInputIso12vCalibrationStore inputModuleCalibrationStore{eeprom,
                                                                            engine_mon::moduleCalibrationOffset};

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
// Status LED (green, see showStatus()): dim and steady = all good; full
// brightness = something needs attention. The red half of the enclosure LED
// is not used (its pin becomes the buzzer).
slstm32::drivers::PwmLedDriver statusLed{runtime, engine_mon::board::greenLed()};
slstm32::drivers::ToneDriver buzzer{runtime, engine_mon::board::buzzer()};
can_mcu::RtcClock rtc{logger};
std::uint32_t identifyUntil{};
bool applicationFailed{};
bool clickPending{};

// Digital inputs as last logged (12 V present), and the gear position.
bool inputsLogged[engine_mon::board::digitalInputCount]{};
bool inputsKnown{};
enum class Gear : std::uint8_t { neutral, forward, reverse, invalid };
Gear loggedGear{Gear::neutral};

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
// Dry exhaust: CT1780 K-type thermocouple converter on 1-Wire bus 0 (PB12),
// one reading per second. Default alarms are placeholders until the engine's
// normal exhaust temperatures are known (tune over CAN): a warning above
// 550 °C over 30 s, a missing or open thermocouple a warning after 10 s.
constexpr sensors::AlarmRule dryExhaustAlarms[]{
    {ValueFunction::average, Comparison::above, Severity::warning, false, {30u, false}, 10u, 550000, 0, 20000},
};
constexpr sensors::MeasurementConfig dryExhaustChannelConfig{
    "dry-exhaust", sensors::Quantity::temperature, 2u, 10000u, 0u, dryExhaustAlarms,
    sizeof dryExhaustAlarms / sizeof dryExhaustAlarms[0], {true, Severity::warning, 10u, false}};
sensors::Measurement dryExhaustChannel{dryExhaustChannelConfig};
can_mcu::OneWirePin oneWirePin0{engine_mon::board::oneWire0Port, engine_mon::board::oneWire0Pin};
slstm32::drivers::OneWireBus oneWire0{runtime, oneWirePin0.hardware()};
constexpr std::uint32_t dryExhaustPeriodMs = 1000u;
slstm32::drivers::Ct1780Driver dryExhaustSensor{runtime, oneWire0, logger, &dryExhaustChannel, nullptr,
                                                dryExhaustPeriodMs};

// PT100 probes (350 °C) through DFRobot SEN0198 amplifiers on the ADC
// (PA4..PA6, sampled by the McuMonitor), one reading per second each. Bridge
// constants of DFRobot's library (verified on LT1; calibrate with two
// points). Below 50 mV or above 3.2 V the probe, the amplifier, or its
// supply is missing (an open input floats inside that range: the host needs
// pull-downs). Default alarms are placeholders until the engine's normal
// values are known (tune over CAN): wet exhaust is the raw water failure
// alarm (warning above 70 °C, alarm above 85 °C); alternator and heat
// exchanger warn above 90 °C and 95 °C.
constexpr sensors::RtdBridge sen0198Bridge{1800.0f, 3976.2f, 39.762f};
const slstm32::drivers::AnalogSensorConfig pt100Config{sensors::rtdBridgeConversion(sen0198Bridge), 50, 3200, 1000u};
constexpr sensors::FaultRule pt100Fault{true, Severity::warning, 10u, false};
constexpr sensors::AlarmRule wetExhaustAlarms[]{
    {ValueFunction::average, Comparison::above, Severity::warning, false, {10u, false}, 5u, 70000, 0, 3000},
    {ValueFunction::average, Comparison::above, Severity::alarm, false, {5u, false}, 3u, 85000, 0, 3000},
};
constexpr sensors::AlarmRule alternatorAlarms[]{
    {ValueFunction::average, Comparison::above, Severity::warning, false, {60u, false}, 10u, 90000, 0, 3000},
};
constexpr sensors::AlarmRule heatExchangerAlarms[]{
    {ValueFunction::average, Comparison::above, Severity::warning, false, {30u, false}, 10u, 95000, 0, 3000},
};
constexpr sensors::MeasurementConfig wetExhaustChannelConfig{
    "wet-exhaust", sensors::Quantity::temperature, 3u, 10000u, 0u, wetExhaustAlarms,
    sizeof wetExhaustAlarms / sizeof wetExhaustAlarms[0], pt100Fault};
constexpr sensors::MeasurementConfig alternatorChannelConfig{
    "alternator", sensors::Quantity::temperature, 4u, 10000u, 0u, alternatorAlarms,
    sizeof alternatorAlarms / sizeof alternatorAlarms[0], pt100Fault};
constexpr sensors::MeasurementConfig heatExchangerChannelConfig{
    "heat-exchanger", sensors::Quantity::temperature, 5u, 10000u, 0u, heatExchangerAlarms,
    sizeof heatExchangerAlarms / sizeof heatExchangerAlarms[0], pt100Fault};
sensors::Measurement wetExhaustChannel{wetExhaustChannelConfig};
sensors::Measurement alternatorChannel{alternatorChannelConfig};
sensors::Measurement heatExchangerChannel{heatExchangerChannelConfig};
// McuMonitor input indices 0..2 = AI0..AI2 (added in this order at init).
slstm32::drivers::AnalogSensor wetExhaustSensor{runtime, mcu.inputHardware(0u), pt100Config, &wetExhaustChannel,
                                                sensors::Identity::analog(engine_mon::board::analogChannels[0])};
slstm32::drivers::AnalogSensor alternatorSensor{runtime, mcu.inputHardware(1u), pt100Config, &alternatorChannel,
                                                sensors::Identity::analog(engine_mon::board::analogChannels[1])};
slstm32::drivers::AnalogSensor heatExchangerSensor{runtime, mcu.inputHardware(2u), pt100Config,
                                                   &heatExchangerChannel,
                                                   sensors::Identity::analog(engine_mon::board::analogChannels[2])};

sensors::Measurement* const channels[]{&boardChannel,      &mcuChannel,        &dryExhaustChannel,
                                       &wetExhaustChannel, &alternatorChannel, &heatExchangerChannel};

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
    {&dryExhaustChannel, 3u, b1s::Publish::b1s, static_cast<std::uint8_t>(n2k::TemperatureSource::exhaustGas), "",
     "Dry exhaust"},
    {&wetExhaustChannel, 4u, b1s::Publish::b1s, static_cast<std::uint8_t>(n2k::TemperatureSource::exhaustGas), "",
     "Wet exhaust"},
    {&alternatorChannel, 5u, b1s::Publish::b1s, engineRoom, "", "Alternator"},
    {&heatExchangerChannel, 6u, b1s::Publish::b1s, engineRoom, "", "Heat exchanger"},
};
b1s::ChannelSettings::Override b1sOverrides[engine_mon::b1sChannelSlots]{};
b1s::ChannelSettings b1sSettings{eeprom, engine_mon::b1sSettingsOffset, b1sOverrides};
b1s::ChannelState b1sStates[sizeof b1sChannels / sizeof b1sChannels[0]]{};
b1s::MeasurementPublisher measurementPublisher{runtime, n2kNode, b1sChannels, b1sStates, 2000u, logger, &b1sSettings};

// Alarm sounds on the passive buzzer, one per severity, all at full volume
// (an engine room is loud) and near the buzzer's 2.7 kHz resonance, where it
// is loudest: two chirps every 30 s ("have a look"), three beeps every 3 s
// ("act"), a continuous two-tone siren ("act now"). They play while an
// alarm waits for acknowledgement (BF5 or BF6).
using slstm32::drivers::ToneStep;
constexpr ToneStep warningSteps[]{{2700u, 60u, 1.0f}, {0u, 120u}, {2700u, 60u, 1.0f}, {0u, 30000u}};
constexpr ToneStep alarmSteps[]{{2700u, 150u, 1.0f}, {0u, 150u}, {2700u, 150u, 1.0f},
                                {0u, 150u},          {2700u, 150u, 1.0f}, {0u, 2500u}};
constexpr ToneStep emergencySteps[]{{2550u, 250u, 1.0f}, {2850u, 250u, 1.0f}};
constexpr slstm32::drivers::TonePattern warningSound{warningSteps, sizeof warningSteps / sizeof warningSteps[0], true};
constexpr slstm32::drivers::TonePattern alarmSound{alarmSteps, sizeof alarmSteps / sizeof alarmSteps[0], true};
constexpr slstm32::drivers::TonePattern emergencySound{emergencySteps, sizeof emergencySteps / sizeof emergencySteps[0],
                                                       true};
// Feedback: started (rising two-tone).
constexpr ToneStep startupSteps[]{{2200u, 70u, 0.05f}, {0u, 30u}, {2900u, 90u, 0.05f}};
constexpr slstm32::drivers::TonePattern startupSound{startupSteps, sizeof startupSteps / sizeof startupSteps[0], false};
slstm32::drivers::TonePatternPlayer tonePlayer{runtime, buzzer};
const sensors::AlarmAnnunciator::Config annunciatorConfig{{&warningSound, &alarmSound, &emergencySound}, nullptr,
                                                          nullptr};
sensors::AlarmAnnunciator annunciator{measurements, tonePlayer, annunciatorConfig};

slstm32::CommandConsole console{can_mcu::vcpInput(), logger};
can_mcu::HealthService health{runtime, logger, eeprom, engine_mon::healthRecordOffset, mcu};
engine_mon::IoConsole ioConsole{logger, inputModules, outputs, mcu, analogInputs};
// Stored calibrations of the modules' analog inputs; console "ain", "cal".
semantic_modules::AdInputIso12vCalibrator inputModuleCalibrator{
    runtime, logger, inputModuleCalibrationStore, inputModules, sizeof inputModules / sizeof inputModules[0]};

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
struct Ad1;
struct Ad2;
struct InputModuleCalibrator;
struct Do0;
struct Do1;
struct Do2;
struct Mcu;
struct BoardTemperature;
struct DryExhaustSensor;
struct WetExhaustSensor;
struct AlternatorSensor;
struct HeatExchangerSensor;
struct Can;
struct CanActivityLed;
struct StatusLed;
struct Buzzer;
struct TonePlayer;
struct Annunciator;
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
        slstm32::bind<component::Ad1>(ad1),
        slstm32::bind<component::Ad2>(ad2),
        slstm32::bind<component::Do0>(do0),
        slstm32::bind<component::Do1>(do1),
        slstm32::bind<component::Do2>(do2),
        slstm32::bind<component::Mcu>(mcu),
        slstm32::bind<component::BoardTemperature>(boardTemperature),
        slstm32::bind<component::DryExhaustSensor>(dryExhaustSensor),
        slstm32::bind<component::WetExhaustSensor>(wetExhaustSensor),
        slstm32::bind<component::AlternatorSensor>(alternatorSensor),
        slstm32::bind<component::HeatExchangerSensor>(heatExchangerSensor),
        slstm32::bind<component::Can>(can),
        slstm32::bind<component::CanActivityLed>(canActivityLed),
        slstm32::bind<component::StatusLed>(statusLed),
        slstm32::bind<component::Buzzer>(buzzer)),
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
        slstm32::bind<component::TonePlayer>(tonePlayer),
        slstm32::bind<component::Annunciator>(annunciator),
        slstm32::bind<component::InputModuleCalibrator>(inputModuleCalibrator),
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
    logger.writef(LogLevel::info, "button: %s %s",
                  event.id < engine_mon::board::buttonCount ? engine_mon::board::buttonNames[event.id] : "?",
                  event.pressed ? "pressed" : "released");
    // A quiet click as feedback, played after the annunciator has run (it
    // may stop its alarm sound in this pass, which would cut the click).
    if (event.pressed) clickPending = true;
    if (event.pressed && measurements.summary().unacknowledged) measurements.acknowledgeAll();
}

const char* gearName(Gear gear) {
    switch (gear) {
    case Gear::neutral: return "neutral";
    case Gear::forward: return "forward";
    case Gear::reverse: return "reverse";
    case Gear::invalid: return "forward and reverse at once (check the reed switches)";
    }
    return "?";
}

bool digitalOn(std::size_t index) {
    constexpr auto perModule = semantic_modules::AdInputIso12v::channels;
    return inputModules[index / perModule]->digitalActive(index % perModule);
}

// Analog inputs of AD1/AD2 as last logged (mV; INT32_MIN = no value).
constexpr std::int32_t analogUnknown = INT32_MIN;
constexpr std::int32_t analogLogStepMv = 250;
std::int32_t analogLogged[engine_mon::board::moduleAnalogCount]{};

void logAnalog(std::size_t index, std::int32_t millivolts) {
    if (millivolts == analogUnknown) {
        logger.writef(LogLevel::debug, "input: %s -", engine_mon::board::moduleAnalogNames[index]);
        return;
    }
    const auto magnitude = millivolts < 0 ? -millivolts : millivolts;
    logger.writef(LogLevel::debug, "input: %s %s%ld.%02ld V", engine_mon::board::moduleAnalogNames[index],
                  millivolts < 0 ? "-" : "", static_cast<long>(magnitude / 1000),
                  static_cast<long>(magnitude % 1000 / 10));
}

// Logs every change of a digital input and every analog change of at least
// 0.25 V (debug), the gear position from the two gearbox reed switches on
// change (info), and the state at startup once (info).
void watchInputs() {
    using engine_mon::board::digitalInputCount;
    using engine_mon::board::digitalInputNames;
    using engine_mon::board::moduleAnalogCount;
    constexpr auto perModule = semantic_modules::AdInputIso12v::channels;
    for (std::size_t index = 0u; index < digitalInputCount; ++index) {
        const bool on = digitalOn(index);
        if (inputsKnown && on != inputsLogged[index]) {
            logger.writef(LogLevel::debug, "input: %s %s", digitalInputNames[index], on ? "12 V" : "off");
        }
        inputsLogged[index] = on;
    }
    for (std::size_t index = 0u; index < moduleAnalogCount; ++index) {
        const auto& module = *inputModules[index / perModule];
        const auto channel = index % perModule;
        const auto value = module.analogValid(channel) ? module.analogMillivolts(channel) : analogUnknown;
        const auto last = analogLogged[index];
        const bool changed = (value == analogUnknown) != (last == analogUnknown) ||
                             (value != analogUnknown && (value - last >= analogLogStepMv || last - value >= analogLogStepMv));
        if (!inputsKnown || changed) {
            // The first value after startup (or after the module answers
            // again) is not a change.
            if (inputsKnown && last != analogUnknown) logAnalog(index, value);
            analogLogged[index] = value;
        }
    }
    const bool reverse = inputsLogged[engine_mon::board::gearReverseInput];
    const bool forward = inputsLogged[engine_mon::board::gearForwardInput];
    const auto gear = forward && reverse ? Gear::invalid : forward ? Gear::forward : reverse ? Gear::reverse : Gear::neutral;
    if (!inputsKnown || gear != loggedGear) {
        logger.writef(gear == Gear::invalid ? LogLevel::warning : LogLevel::info, "gearbox: %s", gearName(gear));
        loggedGear = gear;
    }
    if (!inputsKnown) {
        char text[digitalInputCount + 1u]{};
        for (std::size_t index = 0u; index < digitalInputCount; ++index) text[index] = inputsLogged[index] ? '1' : '0';
        logger.writef(LogLevel::info, "input: D10..D13 D20..D23 = %.4s %.4s (1 = 12 V)", text, text + 4);
        inputsKnown = true;
    }
}

// Status LED: one green LED says how the engine monitor is.
//   dim, steady (1 %) .............. all good
//   full, blinking ................. a measurement alarm waits for
//                                    acknowledgement: 0.5 Hz warning,
//                                    1 Hz alarm, 4 Hz emergency
//   full, steady ................... alarms acknowledged but still active
//   full, 5 Hz ..................... identify (over CAN), or the
//                                    application failed to start
enum class Status : std::uint8_t { ok, warning, alarm, emergency, acknowledged, identify, failed };

Status currentStatus() {
    if (identifyUntil != 0u) return Status::identify;
    if (applicationFailed) return Status::failed;
    const auto summary = measurements.summary();
    if (summary.unacknowledged) {
        switch (summary.highestUnacknowledged) {
        case Severity::warning: return Status::warning;
        case Severity::alarm: return Status::alarm;
        case Severity::emergency: return Status::emergency;
        }
    }
    return summary.active ? Status::acknowledged : Status::ok;
}

void showStatus() {
    static bool shown{};
    static Status last{};
    const auto status = currentStatus();
    if (shown && status == last) return;
    shown = true;
    last = status;
    // The enclosure LED is bright: 1 % is "dim", full duty "look here".
    statusLed.setBrightness(status == Status::ok ? 0.01f : 1.0f);
    switch (status) {
    case Status::ok:
    case Status::acknowledged: statusLed.on(); break;
    case Status::warning: statusLed.blink(2000u, 1000u); break;
    case Status::alarm: statusLed.blink(1000u, 500u); break;
    case Status::emergency: statusLed.blink(250u, 125u); break;
    case Status::identify:
    case Status::failed: statusLed.blink(200u, 100u); break;
    }
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

// Device commands over CAN: identify (status LED and yellow CAN LED blink
// fast, one quiet beep),
// restart, and restart into the bootloader (both after the reply has gone out).
b1s::CommandResult runDeviceCommand(void*, b1s::Command command, std::uint8_t, std::int32_t argument) {
    switch (command) {
    case b1s::Command::identify: {
        const auto durationMs = static_cast<std::uint32_t>(argument & 0xFFFF) * 1000u;
        canMonitor.identify(durationMs);
        identifyUntil = durationMs == 0u ? 0u : runtime.millis() + durationMs;
        if (durationMs != 0u) buzzer.play(2700u, 80u, 0.05f);
        return b1s::CommandResult::done;
    }
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
        !ioConsole.registerCommands(console) || !rtc.registerCommands(console) ||
        !inputModuleCalibrator.registerCommands(console)) {
        logger.write(LogLevel::error, "console: commands not registered");
    }
    if (!application.init()) {
        logger.write(LogLevel::error, "application init failed");
        applicationFailed = true;
    }
    rtc.logState();
    // Started: a short, quiet rising two-tone (the first tone now, the second
    // from the tone pattern player).
    if (!applicationFailed) tonePlayer.play(startupSound);
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
    watchInputs();
    if (clickPending) {
        clickPending = false;
        buzzer.play(2700u, 25u, 0.05f);
    }
    const auto now = runtime.millis();
    if (identifyUntil != 0u && static_cast<std::int32_t>(now - identifyUntil) >= 0) identifyUntil = 0u;
    showStatus();
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

extern "C" void engine_mon_app_tick(void) {
    // Keep short tone deadlines accurate while the foreground is busy.
    buzzer.onTick();
}

extern "C" void HAL_GPIO_EXTI_Callback(std::uint16_t pin) {
    (void)buttons.onInterrupt(pin);
}
