#pragma once

// GENERATED from src/lib/b1s/registry.yaml by src/lib/b1s/tools/generate_registry.py.
// Do not edit: change the registry and regenerate (rules: src/lib/b1s/README.md,
// "Adding to B1S").

#include <cstdint>

namespace b1s {

inline constexpr std::uint16_t manufacturerCode = 2046u;
inline constexpr std::uint8_t industryCode = 4u;
inline constexpr std::uint16_t proprietaryHeader = 0x9FFEu; // sent as FE 9F

// B1S proprietary PGNs.
namespace pgn {
inline constexpr std::uint32_t measurement = 65280u; // single frame, broadcast
inline constexpr std::uint32_t outputStatus = 65281u; // single frame, broadcast
inline constexpr std::uint32_t measurementWide = 130816u; // fast packet, broadcast
inline constexpr std::uint32_t measurementSummary = 130817u; // fast packet, broadcast
inline constexpr std::uint32_t event = 130818u; // fast packet, broadcast
inline constexpr std::uint32_t channelDescription = 130819u; // fast packet, broadcast
inline constexpr std::uint32_t command = 61184u; // single frame, addressed
inline constexpr std::uint32_t service = 126720u; // fast packet, addressed
} // namespace pgn

// Quantities of the measurement messages; units and scales below.
enum class Quantity : std::uint8_t {
    temperature = 1u,
    relativeHumidity = 2u,
    pressure = 3u,
    voltage = 4u,
    current = 5u,
    power = 6u,
    energy = 7u,
    frequency = 8u,
    rotationalSpeed = 9u,
    illuminance = 10u,
    uvIndex = 11u,
    co2 = 12u,
    angle = 13u,
    heading = 14u,
    pitch = 15u,
    roll = 16u,
    angularVelocity = 17u,
    acceleration = 18u,
    distance = 19u,
    speed = 20u,
    ratio = 21u,
    count = 22u,
    duration = 23u,
};

// Physical value = raw x scale, in the quantity's SI unit.
constexpr double scale(Quantity quantity) {
    switch (quantity) {
    case Quantity::temperature: return 0.001; // K
    case Quantity::relativeHumidity: return 1e-06; // ratio
    case Quantity::pressure: return 0.1; // Pa
    case Quantity::voltage: return 0.001; // V
    case Quantity::current: return 0.001; // A
    case Quantity::power: return 0.001; // W
    case Quantity::energy: return 1.0; // J
    case Quantity::frequency: return 0.001; // Hz
    case Quantity::rotationalSpeed: return 0.001; // Hz
    case Quantity::illuminance: return 0.001; // lx
    case Quantity::uvIndex: return 0.001; // 1
    case Quantity::co2: return 1e-09; // ratio
    case Quantity::angle: return 1e-06; // rad
    case Quantity::heading: return 1e-06; // rad
    case Quantity::pitch: return 1e-06; // rad
    case Quantity::roll: return 1e-06; // rad
    case Quantity::angularVelocity: return 1e-06; // rad/s
    case Quantity::acceleration: return 0.0001; // m/s2
    case Quantity::distance: return 0.001; // m
    case Quantity::speed: return 0.001; // m/s
    case Quantity::ratio: return 1e-06; // ratio
    case Quantity::count: return 1.0; // 1
    case Quantity::duration: return 0.001; // s
    }
    return 0.0;
}

// True for quantities encoded as float32 instead of scaled int32.
constexpr bool isFloat(Quantity quantity) {
    static_cast<void>(quantity);
    return false;
}

// Event types of the Event message.
enum class EventType : std::uint8_t {
    lightningStrike = 1u,
};

// Output kinds of Output Status and Channel Description.
enum class OutputKind : std::uint8_t {
    relay = 1u,
    dimmer = 2u,
    fan = 3u,
};

// Commands of the Command message; bit 7 marks a reply.
enum class Command : std::uint8_t {
    identify = 1u,
    setState = 2u,
    setLevel = 3u,
    calibrate = 4u,
    calibrateSecondPoint = 5u,
    resetChannel = 6u,
    acknowledgeAlarms = 7u,
    restart = 8u,
};

// Service codes of the Service message.
enum class Service : std::uint8_t {
    parameterList = 1u,
    parameterRead = 2u,
    parameterWrite = 3u,
    console = 4u,
};

// Parameters for configuration over CAN (scope, type, unit: registry).
enum class Parameter : std::uint16_t {
    deviceRole = 1u,
    preferredAddress = 3u,
    channelLocation = 16u,
    channelLabel = 17u,
    channelEncoding = 18u,
    channelStandardInstance = 19u,
    calibrationOffset = 32u,
    calibrationGain = 33u,
    alarmThreshold0 = 40u,
    alarmThreshold1 = 41u,
    alarmThreshold2 = 42u,
    alarmThreshold3 = 43u,
    alarmRule0 = 44u,
    alarmRule1 = 45u,
    alarmRule2 = 46u,
    alarmRule3 = 47u,
    uptime = 64u,
};

// True for parameters of a channel (addressed with a channel number).
constexpr bool isChannelParameter(Parameter parameter) {
    switch (parameter) {
    case Parameter::channelLocation:
    case Parameter::channelLabel:
    case Parameter::channelEncoding:
    case Parameter::channelStandardInstance:
    case Parameter::calibrationOffset:
    case Parameter::calibrationGain:
    case Parameter::alarmThreshold0:
    case Parameter::alarmThreshold1:
    case Parameter::alarmThreshold2:
    case Parameter::alarmThreshold3:
    case Parameter::alarmRule0:
    case Parameter::alarmRule1:
    case Parameter::alarmRule2:
    case Parameter::alarmRule3:
        return true;
    default:
        return false;
    }
}

// Category of a standard NMEA 2000 PGN (registry: standard_pgns).
enum class StandardCategory : std::uint8_t { unlisted, display, navigation };

constexpr StandardCategory standardCategory(std::uint32_t pgn) {
    switch (pgn) {
    case 127488u: // Engine Parameters Rapid Update
    case 127501u: // Binary Switch Bank Status
    case 127508u: // Battery Status
    case 130313u: // Humidity
    case 130314u: // Actual Pressure
    case 130316u: // Temperature Extended Range
        return StandardCategory::display;
    case 126983u: // Alert
    case 126984u: // Alert Response
    case 126985u: // Alert Text
    case 126992u: // System Time
    case 127245u: // Rudder
    case 127250u: // Vessel Heading
    case 127251u: // Rate of Turn
    case 127257u: // Attitude
    case 127258u: // Magnetic Variation
    case 128259u: // Speed
    case 128267u: // Water Depth
    case 129025u: // Position Rapid Update
    case 129026u: // COG and SOG Rapid Update
    case 129029u: // GNSS Position Data
    case 130306u: // Wind Data
        return StandardCategory::navigation;
    default:
        return StandardCategory::unlisted;
    }
}

// Device types: product code (PGN 126996), preferred address, NMEA 2000
// device class and function.
struct Product {
    std::uint16_t code;
    std::uint8_t preferredAddress;
    std::uint8_t deviceClass;
    std::uint8_t deviceFunction;
};

namespace product {
inline constexpr Product labTest01{1u, 35u, 75u, 130u};
} // namespace product

} // namespace b1s
