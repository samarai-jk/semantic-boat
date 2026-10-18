#pragma once

#include <cstddef>
#include <cstdint>
#include "b1s/measurement_publisher.hpp"
#include "b1s/registry.hpp"
#include "n2k/can_id.hpp"
#include "n2k/node.hpp"
#include "slstm32/console.hpp"
#include "slstm32/drivers/serial_log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/sensors/registry.hpp"
#include "slstm32/service.hpp"

namespace b1s {

// Status of a parameter reply (CONFIGURATION.md).
enum class ParameterStatus : std::uint8_t {
    done = 0u, unknownParameter = 1u, unknownChannel = 2u, invalidValue = 3u, readOnly = 4u, busy = 5u,
    notPermitted = 6u,
};

// Result of a command reply (README, Command).
enum class CommandResult : std::uint8_t {
    done = 0u, unknownCommand = 1u, unknownChannel = 2u, invalidArgument = 3u, refused = 4u, busy = 5u,
};

// A device-level numeric parameter the device provides itself (for example
// preferredAddress). write == nullptr: read-only.
struct DeviceParameter {
    Parameter id{};
    bool (*read)(void* context, std::int32_t& value){};
    ParameterStatus (*write)(void* context, std::int32_t value){};
    void* context{};
};

// Device commands the server does not handle itself (identify, restart, ...):
// returns the result; unknownCommand if the device has none.
using CommandHandler = CommandResult (*)(void* context, Command command, std::uint8_t channel, std::int32_t argument);

struct ServiceServerConfig {
    MeasurementPublisher* publisher{};               // channels, encoding, descriptions
    slstm32::sensors::MeasurementRegistry* measurements{}; // calibration, thresholds, alarms
    slstm32::CommandConsole* console{};              // console over CAN (nullptr: none)
    slstm32::drivers::SerialLogDriver* log{};        // its output, mirrored to the requester
    const DeviceParameter* deviceParameters{};
    std::size_t deviceParameterCount{};
    CommandHandler commands{};
    void* commandContext{};
};

// Configuration, calibration, and diagnosis over CAN (CONFIGURATION.md):
// answers B1S Service messages (parameter list, read, write; console) and
// Command messages addressed to the device. Feed it every message the node
// passes to the application with handleMessage(); it returns true for the
// ones it consumed. One request at a time: a request that arrives while a
// reply is still waiting replaces it (the requester retries).
//
// Log stream (CONFIGURATION.md): the device log goes to one requester as
// console replies of up to 200 characters, at most 512 bytes per second. A
// logStream request starts or extends it for up to 60 minutes (0 stops it);
// a console request runs its line and keeps the stream for at least 30 s.
// The device stops the stream itself when the time is up, so a forgotten or
// crashed client never floods the bus; lines it cannot send in time are
// dropped and marked.
class ServiceServer final : public slstm32::Service {
public:
    ServiceServer(slstm32::Runtime runtime, n2k::Node& node, const ServiceServerConfig& config)
        : runtime_(runtime), node_(node), config_(config) {}

    bool init() override;
    void run() override;
    bool handleMessage(const n2k::CanId& id, const std::uint8_t* data, std::uint8_t length);

private:
    struct Value {
        bool text{};
        std::int32_t number{};
        char string[maxTextLength + 1u]{};
    };
    struct Pending {
        bool active{};
        std::uint8_t service{};
        std::uint8_t sequence{};
        std::uint8_t requester{};
        ParameterStatus status{};
        std::uint16_t parameter{};
        std::uint8_t channel{};
        std::uint8_t first{};
    };

    void handleService(const n2k::CanId& id, const std::uint8_t* data, std::uint8_t length);
    void handleCommand(const n2k::CanId& id, const std::uint8_t* data, std::uint8_t length);
    CommandResult runCommand(Command command, std::uint8_t channel, std::int32_t argument);
    ParameterStatus read(Parameter parameter, std::uint8_t channel, Value& value);
    ParameterStatus write(Parameter parameter, std::uint8_t channel, const Value& value);
    std::size_t parameterIds(std::uint16_t* ids, std::size_t capacity) const;
    const DeviceParameter* deviceParameter(Parameter parameter) const;
    std::size_t measurementIndex(const ChannelConfig& channel) const;
    bool sendReply();
    void startStream(std::uint8_t requester, std::uint8_t sequence, std::uint32_t durationMs);
    void stopStream();
    std::uint32_t streamRemainingMs(std::uint32_t now) const;
    void streamLog(std::uint32_t now);

    slstm32::Runtime runtime_;
    n2k::Node& node_;
    const ServiceServerConfig& config_;
    Pending pending_{};
    // Command reply that did not fit into the transmit queue yet.
    std::uint8_t commandReply_[8]{};
    std::uint8_t commandReplyTo_{n2k::nullAddress};
    // Log stream (console session).
    std::uint8_t streamRequester_{n2k::nullAddress};
    std::uint8_t streamSequence_{};
    std::uint32_t streamUntil_{};
    std::uint32_t streamSentAt_{};
    std::int32_t streamBudget_{};    // bytes that may be sent now (negative: overdrawn)
    std::uint32_t streamLost_{};     // log mirror losses already reported
};

} // namespace b1s
