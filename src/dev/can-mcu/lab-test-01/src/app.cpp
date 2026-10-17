#include "lab_test_01/app.h"
#include "board.hpp"
#include "can_mcu/board.hpp"
#include "can_mcu/board_temperatures.hpp"
#include "can_mcu/can_bus.hpp"
#include "can_mcu/i2c_bus.hpp"
#include "can_mcu/mcu_monitor.hpp"
#include "can_mcu/one_wire_pin.hpp"
#include "can_mcu/supervision.hpp"
#include "can_monitor_service.hpp"
#include "device_settings.hpp"
#include "dev_tools.hpp"
#include "diagnostics_service.hpp"
#include "events.hpp"
#include "health_service.hpp"
#include "i2c_scan_service.hpp"
#include "one_wire_command.hpp"
#include "n2k/attitude_publisher.hpp"
#include "n2k/humidity.hpp"
#include "n2k/measurement_publisher.hpp"
#include "n2k/name.hpp"
#include "n2k/node.hpp"
#include "n2k/pgn.hpp"
#include "n2k/pressure.hpp"
#include "n2k/temperature.hpp"
#include "semantic_modules/ad_input_iso_12v.hpp"
#include "semantic_modules/ad_input_iso_12v_calibration.hpp"
#include "slstm32/console.hpp"
#include "slstm32/application.hpp"
#include "slstm32/build_profile.hpp"
#include "slstm32/drivers/at24c256.hpp"
#include "slstm32/drivers/button.hpp"
#include "slstm32/drivers/can_bus.hpp"
#include "slstm32/drivers/bmp280.hpp"
#include "slstm32/drivers/bmp585.hpp"
#include "slstm32/drivers/bno055.hpp"
#include "slstm32/drivers/dht20.hpp"
#include "slstm32/drivers/one_wire.hpp"
#include "slstm32/drivers/led.hpp"
#include "slstm32/drivers/pwm_led.hpp"
#include "slstm32/drivers/serial_log.hpp"
#include "slstm32/drivers/tmp75.hpp"
#include "slstm32/drivers/tone.hpp"
#include "slstm32/drivers/tone_pattern.hpp"
#include "slstm32/drivers/veml7700.hpp"
#include "slstm32/event_bus.hpp"
#include "slstm32/sensors/alarm_annunciator.hpp"
#include "slstm32/sensors/attitude_summary.hpp"
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
using sensors::AlarmKind;
using sensors::Severity;
using slstm32::drivers::ToneStep;

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
constexpr std::uint32_t n2kTransmitPgns[]{n2k::pgn::vesselHeading, n2k::pgn::rateOfTurn, n2k::pgn::attitude,
                                           n2k::pgn::temperatureExtendedRange, n2k::pgn::humidity, n2k::pgn::actualPressure,
                                           n2k::pgn::batteryStatus, n2k::pgn::binarySwitchBankStatus};
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

// Measurement channels: the board and MCU probes, a DFRobot DHT20 on the
// I2C connectors as reference ("air"), and a DFRobot VEML7700 ambient light
// sensor ("light"), a Bosch BMP585 barometer ("baro", die temperature
// "baro-t"), and the DFRobot SEN0253 10-DOF module: BNO055 orientation
// ("heading", "pitch", "roll") and BMP280 barometer ("baro2", "baro2-t"). No alarm rules while sensors are being
// brought up; the annunciator stays silent without them. PA4 is prepared as a
// 1-Wire bus for the next sensor ("onewire" command).
constexpr sensors::MeasurementConfig boardChannelConfig{
    "board", sensors::Quantity::temperature, 0u, 10000u, 4000u, nullptr, 0u};
constexpr sensors::MeasurementConfig mcuChannelConfig{
    "mcu", sensors::Quantity::temperature, 1u, 10000u, 8000u, nullptr, 0u};
constexpr sensors::MeasurementConfig airChannelConfig{
    "air", sensors::Quantity::temperature, 2u, 10000u, 0u, nullptr, 0u};
constexpr sensors::MeasurementConfig airHumidityChannelConfig{
    "air-rh", sensors::Quantity::relativeHumidity, 0u, 10000u, 0u, nullptr, 0u};
sensors::Measurement boardChannel{boardChannelConfig};
sensors::Measurement mcuChannel{mcuChannelConfig};
sensors::Measurement airChannel{airChannelConfig};
sensors::Measurement airHumidityChannel{airHumidityChannelConfig};
constexpr sensors::MeasurementConfig lightChannelConfig{
    "light", sensors::Quantity::illuminance, 0u, 10000u, 0u, nullptr, 0u};
sensors::Measurement lightChannel{lightChannelConfig};
// Barometric tendency over 3 hours (marine rule of thumb): a fall of more
// than 3 hPa is a warning, more than 6 hPa an alarm (strong wind coming).
// Labels: below 0.1 hPa steady, above 3.5 hPa rapid.
constexpr std::uint32_t baroTrendWindowMs = 3u * 60u * 60u * 1000u;
constexpr sensors::AlarmRule baroAlarms[]{
    {AlarmKind::trendFall, Severity::warning, 3000, 500, 0u, false, false},
    {AlarmKind::trendFall, Severity::alarm, 6000, 500, 0u, false, false},
};
constexpr sensors::MeasurementConfig baroChannelConfig{"baro",
                                                       sensors::Quantity::pressure,
                                                       0u,
                                                       10000u,
                                                       0u,
                                                       baroAlarms,
                                                       sizeof baroAlarms / sizeof baroAlarms[0],
                                                       baroTrendWindowMs,
                                                       100,
                                                       3500};
sensors::TrendPoint baroTrend[19]{}; // one sample every 10 min
constexpr sensors::MeasurementConfig baroTemperatureChannelConfig{
    "baro-t", sensors::Quantity::temperature, 8u, 10000u, 0u, nullptr, 0u};
sensors::Measurement baroChannel{baroChannelConfig};
sensors::Measurement baroTemperatureChannel{baroTemperatureChannelConfig};
constexpr sensors::MeasurementConfig headingChannelConfig{
    "heading", sensors::Quantity::angle, 0u, 10000u, 0u, nullptr, 0u};
constexpr sensors::MeasurementConfig pitchChannelConfig{"pitch", sensors::Quantity::angle, 1u, 10000u, 0u, nullptr, 0u};
constexpr sensors::MeasurementConfig rollChannelConfig{"roll", sensors::Quantity::angle, 2u, 10000u, 0u, nullptr, 0u};
constexpr sensors::MeasurementConfig baro2ChannelConfig{
    "baro2", sensors::Quantity::pressure, 1u, 10000u, 0u, nullptr, 0u};
constexpr sensors::MeasurementConfig baro2TemperatureChannelConfig{
    "baro2-t", sensors::Quantity::temperature, 9u, 10000u, 0u, nullptr, 0u};
sensors::Measurement headingChannel{headingChannelConfig};
sensors::Measurement pitchChannel{pitchChannelConfig};
sensors::Measurement rollChannel{rollChannelConfig};
sensors::Measurement baro2Channel{baro2ChannelConfig};
sensors::Measurement baro2TemperatureChannel{baro2TemperatureChannelConfig};
sensors::Measurement* const channels[]{&boardChannel,       &mcuChannel,   &airChannel,    &airHumidityChannel,
                                       &lightChannel,       &baroChannel,  &baroTemperatureChannel,
                                       &headingChannel,     &pitchChannel, &rollChannel,   &baro2Channel,
                                       &baro2TemperatureChannel};

slstm32::drivers::Dht20Driver dht20{runtime, i2c, logger, &airChannel, &airHumidityChannel};
slstm32::drivers::Veml7700Driver veml7700{runtime, i2c, logger, &lightChannel};
slstm32::drivers::Bmp585Driver bmp585{runtime, i2c, logger, &baroChannel, &baroTemperatureChannel};
slstm32::drivers::Bno055Driver bno055{runtime, i2c, logger, &headingChannel, &pitchChannel, &rollChannel};
slstm32::drivers::Bmp280Driver bmp280{runtime, i2c, logger, &baro2Channel, &baro2TemperatureChannel};
// The BNO055 runs at 10 Hz; the bus gets a 1 s summary (heading, rate of
// turn, attitude) and the log one every 10 s with the extremes and spread.
sensors::AttitudeSummarizer attitudeSummarizer{runtime};
n2k::AttitudePublisher attitudePublisher{n2kNode};
can_mcu::OneWirePin oneWirePin0{can_mcu::GpioPort::a, 4u};
slstm32::drivers::OneWireBus oneWire0{runtime, oneWirePin0.hardware()};
const lab_test_01::OneWirePort oneWirePorts[]{{"PA4", &oneWire0}};

can_mcu::BoardTemperatures boardTemperatures{runtime, boardTemperature, mcu, boardChannel, mcuChannel};
slstm32::storage::KeyedRecordStore::Key sensorSettingsKeys[lab_test_01::sensorSettingsSlots]{};
slstm32::storage::KeyedRecordStore sensorSettings{eeprom, lab_test_01::sensorSettingsOffset, sensorSettingsKeys};
sensors::MeasurementRegistry measurements{runtime, logger, channels, sizeof channels / sizeof channels[0],
                                          &sensorSettings, &events, lab_test_01::alarmChanged};
sensors::MeasurementConsole measurementConsole{measurements, logger};
sensors::MeasurementLog measurementLog{runtime, logger, measurements, 5000u};
const n2k::MeasurementPublication publications[]{
    {&boardChannel, static_cast<std::uint8_t>(n2k::TemperatureSource::inside)},
    {&mcuChannel, static_cast<std::uint8_t>(n2k::TemperatureSource::inside)},
    {&airChannel, static_cast<std::uint8_t>(n2k::TemperatureSource::inside)},
    {&airHumidityChannel, static_cast<std::uint8_t>(n2k::HumiditySource::inside)},
    {&baroChannel, static_cast<std::uint8_t>(n2k::PressureSource::atmospheric)},
};
n2k::MeasurementPublisher measurementPublisher{runtime, n2kNode, publications,
                                               sizeof publications / sizeof publications[0]};


// Alarm sounds on the passive buzzer (loudest near 2.7 kHz), one per
// severity: a quiet "have a look" (the fridge is warm), firm beeps that ask
// for action, and a full-level siren for "the engine is about to fail".
constexpr ToneStep warningSteps[]{{2700u, 40u, 0.05f}, {0u, 120u}, {2700u, 40u, 0.05f}, {0u, 30000u}};
constexpr ToneStep alarmSteps[]{{2700u, 150u, 0.3f}, {0u, 150u}, {2700u, 150u, 0.3f},
                                {0u, 150u},          {2700u, 150u, 0.3f}, {0u, 2500u}};
constexpr ToneStep emergencySteps[]{{2400u, 250u, 1.0f}, {3000u, 250u, 1.0f}};
constexpr slstm32::drivers::TonePattern warningSound{warningSteps, sizeof warningSteps / sizeof warningSteps[0], true};
constexpr slstm32::drivers::TonePattern alarmSound{alarmSteps, sizeof alarmSteps / sizeof alarmSteps[0], true};
constexpr slstm32::drivers::TonePattern emergencySound{emergencySteps, sizeof emergencySteps / sizeof emergencySteps[0],
                                                       true};
slstm32::drivers::TonePatternPlayer tonePlayer{runtime, buzzer};
void showAlarms(void*, const sensors::AlarmSummary& summary);
const sensors::AlarmAnnunciator::Config annunciatorConfig{
    {&warningSound, &alarmSound, &emergencySound}, &showAlarms, nullptr};
sensors::AlarmAnnunciator annunciator{measurements, tonePlayer, annunciatorConfig};
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
struct Dht20;
struct Veml7700;
struct Bmp585;
struct Bno055;
struct Bmp280;
struct AttitudeSummarizer;
struct MeasurementLog;
struct Can;
struct CanActivityLed;
struct InputModule;
struct Diagnostics;
struct I2cScan;
struct N2kNode;
struct CanMonitor;
struct BoardTemperatures;
struct Measurements;
struct MeasurementPublisher;
struct Annunciator;
struct TonePlayer;
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
        slstm32::bind<component::Dht20>(dht20),
        slstm32::bind<component::Veml7700>(veml7700),
        slstm32::bind<component::Bmp585>(bmp585),
        slstm32::bind<component::Bno055>(bno055),
        slstm32::bind<component::Bmp280>(bmp280),
        slstm32::bind<component::Can>(can),
        slstm32::bind<component::CanActivityLed>(canActivityLed),
        slstm32::bind<component::InputModule>(inputModule)),
    slstm32::makeServiceSet(
        slstm32::bind<component::Health>(health),
        slstm32::bind<component::Diagnostics>(diagnostics),
        slstm32::bind<component::I2cScan>(i2cScan),
        slstm32::bind<component::N2kNode>(n2kNode),
        slstm32::bind<component::CanMonitor>(canMonitor),
        slstm32::bind<component::BoardTemperatures>(boardTemperatures),
        slstm32::bind<component::Measurements>(measurements),
        slstm32::bind<component::AttitudeSummarizer>(attitudeSummarizer),
        slstm32::bind<component::MeasurementLog>(measurementLog),
        slstm32::bind<component::MeasurementPublisher>(measurementPublisher),
        slstm32::bind<component::Annunciator>(annunciator),
        slstm32::bind<component::TonePlayer>(tonePlayer),
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

// Red LED for measurement alarms: blinking while unacknowledged, faster the
// more severe; steady while acknowledged alarms are still active.
void showAlarms(void*, const sensors::AlarmSummary& summary) {
    if (summary.unacknowledged) {
        switch (summary.highestUnacknowledged) {
        case Severity::warning: redLed.blink(2000u, 100u); break;
        case Severity::alarm: redLed.blink(500u, 250u); break;
        case Severity::emergency: redLed.blink(160u, 80u); break;
        }
    } else if (summary.active) {
        redLed.on();
    } else {
        redLed.off();
    }
}

// BNO055 sample -> summary. Assumes the chip lies flat with its z axis up:
// heading clockwise, yaw rate = -gyro z (turning to starboard positive).
// Pitch and roll signs follow the chip until the mounting is decided.
void onOrientationSample(void*, const slstm32::drivers::Bno055Driver& sensor) {
    using Mode = slstm32::drivers::Bno055Driver::Mode;
    const bool absolute = sensor.mode() == Mode::ndof || sensor.mode() == Mode::ndofFmcOff;
    attitudeSummarizer.add({static_cast<std::int32_t>(sensor.heading16()) * 1000 / 16,
                            static_cast<std::int32_t>(sensor.pitch16()) * 1000 / 16,
                            static_cast<std::int32_t>(sensor.roll16()) * 1000 / 16,
                            -static_cast<std::int32_t>(sensor.angularRate().z) * 1000 / 16,
                            absolute && sensor.calibration().magnetometer >= 2u});
}

void onAttitudeSummary(void*, const sensors::AttitudeSummary& summary) {
    (void)attitudePublisher.publish(summary);
    static std::uint32_t count = 0u;
    if (++count % 10u != 0u) return;
    char h[20], pm[20], pl[20], ph[20], pd[20], rm[20], rl[20], rh[20], rd[20], ym[20], yx[20];
    logger.writef(LogLevel::info, "attitude: heading %s%s | pitch %s (%s..%s, sd %s) | roll %s (%s..%s, sd %s)",
                  sensors::formatMilli(h, summary.heading, 1u), summary.headingValid ? "" : " (not valid)",
                  sensors::formatMilli(pm, summary.pitchMean, 1u), sensors::formatMilli(pl, summary.pitchMinimum, 1u),
                  sensors::formatMilli(ph, summary.pitchMaximum, 1u), sensors::formatMilli(pd, summary.pitchDeviation, 2u),
                  sensors::formatMilli(rm, summary.rollMean, 1u), sensors::formatMilli(rl, summary.rollMinimum, 1u),
                  sensors::formatMilli(rh, summary.rollMaximum, 1u), sensors::formatMilli(rd, summary.rollDeviation, 2u));
    logger.writef(LogLevel::info, "attitude: yaw rate %s deg/s (max %s), %lu samples",
                  sensors::formatMilli(ym, summary.yawRateMean, 1u), sensors::formatMilli(yx, summary.yawRateMaximum, 1u),
                  static_cast<unsigned long>(summary.samples));
}

// Any button press acknowledges the measurement alarms.
void onButton(slstm32::EventId, const void* payload, std::uint8_t size, void*) {
    if (size != sizeof(slstm32::drivers::ButtonEvent)) return;
    const auto& event = *static_cast<const slstm32::drivers::ButtonEvent*>(payload);
    if (event.pressed && measurements.summary().unacknowledged) measurements.acknowledgeAll();
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
    baroChannel.setTrendBuffer(baroTrend, sizeof baroTrend / sizeof baroTrend[0]);
    bno055.setSampleListener(&onOrientationSample, nullptr);
    attitudeSummarizer.setListener(&onAttitudeSummary, nullptr);
    n2kNode.onMessage(&lab_test_01::CanMonitorService::onMessage, &canMonitor);
    n2kNode.onAddressChange(&onN2kAddressChange, nullptr);
    inputModuleCalibrator.onAutoEvent(&onCalibrationEvent, nullptr);
    if (!events.subscribe(lab_test_01::buttonPressed, &onButton, nullptr)) {
        logger.write(LogLevel::error, "events: alarm acknowledge button not subscribed");
    }
    if (!inputModuleCalibrator.registerCommands(console) || !diagnostics.registerCommands(console) ||
        !health.registerCommands(console) || !measurementConsole.registerCommands(console) ||
        !lab_test_01::registerOneWireCommand(console, oneWirePorts, sizeof oneWirePorts / sizeof oneWirePorts[0]) ||
        !oneWire0.init()) {
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
