#include "b1s/service_server.hpp"
#include "b1s/quantity.hpp"
#include "n2k/bytes.hpp"

namespace b1s {

namespace {

using slstm32::sensors::AlarmKind;
using Result = slstm32::sensors::MeasurementRegistry::CalibrationResult;

constexpr std::uint8_t replyBit = 0x80u;
constexpr std::uint8_t typeNumber = 0u;
constexpr std::uint8_t typeText = 1u;
constexpr std::uint8_t servicePriority = 6u;
constexpr std::uint8_t commandPriority = 3u;
constexpr std::size_t maxListed = 32u;
constexpr std::size_t streamChunk = 200u;
constexpr std::uint32_t consoleSessionMs = 30000u;
constexpr std::uint32_t maxStreamMinutes = 60u;
constexpr std::uint32_t streamIntervalMs = 50u;
// 512 bytes per second: about 75 frames per second of a bus that carries
// about 1,700 (README, bus load). Bursts up to one chunk.
constexpr std::uint32_t streamBytesPerInterval = 512u * streamIntervalMs / 1000u;
constexpr std::uint32_t streamBurstBytes = streamChunk;
constexpr char droppedMarker[] = "[log stream: lines dropped]\r\n";

constexpr Parameter deviceTextParameters[]{Parameter::deviceRole};
constexpr Parameter channelParameters[]{Parameter::channelLocation, Parameter::channelLabel,
                                        Parameter::channelEncoding, Parameter::channelStandardInstance,
                                        Parameter::channelStandardSource};
constexpr Parameter measurementParameters[]{
    Parameter::calibrationOffset, Parameter::calibrationGain, Parameter::alarmThreshold0, Parameter::alarmThreshold1,
    Parameter::alarmThreshold2,   Parameter::alarmThreshold3, Parameter::alarmRule0,      Parameter::alarmRule1,
    Parameter::alarmRule2,        Parameter::alarmRule3,
};

bool reached(std::uint32_t now, std::uint32_t deadline) { return static_cast<std::int32_t>(now - deadline) >= 0; }

std::size_t ruleOf(Parameter parameter, Parameter first) {
    return static_cast<std::size_t>(static_cast<std::uint16_t>(parameter) - static_cast<std::uint16_t>(first));
}

bool absoluteRule(AlarmKind kind) { return kind == AlarmKind::high || kind == AlarmKind::low; }

} // namespace

bool ServiceServer::init() { return config_.publisher != nullptr; }

bool ServiceServer::handleMessage(const n2k::CanId& id, const std::uint8_t* data, std::uint8_t length) {
    if (id.destination != node_.address() || id.source > n2k::maximumAddress || !isB1s(data, length)) return false;
    if (id.pgn == pgn::service) {
        handleService(id, data, length);
        return true;
    }
    if (id.pgn == pgn::command) {
        handleCommand(id, data, length);
        return true;
    }
    return false;
}

void ServiceServer::handleService(const n2k::CanId& id, const std::uint8_t* data, std::uint8_t length) {
    if (length < 4u || (data[2] & replyBit) != 0u) return;
    Pending request{};
    request.active = true;
    request.service = data[2];
    request.sequence = data[3];
    request.requester = id.source;
    switch (static_cast<b1s::Service>(data[2])) {
    case b1s::Service::parameterList:
        request.first = length > 4u ? data[4] : 0u;
        break;
    case b1s::Service::parameterRead:
    case b1s::Service::parameterWrite: {
        if (length < 7u) return;
        request.parameter = static_cast<std::uint16_t>(data[4] | data[5] << 8u);
        request.channel = data[6];
        if (request.service == static_cast<std::uint8_t>(b1s::Service::parameterWrite)) {
            Value value{};
            if (length < 9u) return;
            value.text = data[7] == typeText;
            if (value.text) {
                const std::size_t textLength = data[8] >= 2u ? data[8] - 2u : 0u;
                if (9u + 1u + textLength > length || textLength > maxTextLength) return;
                for (std::size_t index = 0u; index < textLength; ++index) {
                    value.string[index] = static_cast<char>(data[10u + index]);
                }
            } else {
                if (length < 12u) return;
                value.number = static_cast<std::int32_t>(data[8] | data[9] << 8u | data[10] << 16u |
                                                         static_cast<std::uint32_t>(data[11]) << 24u);
            }
            request.status = write(static_cast<Parameter>(request.parameter), request.channel, value);
        }
        break;
    }
    case b1s::Service::console: {
        if (!config_.console || !config_.log) return;
        char line[slstm32::CommandConsole::lineCapacity + 1u]{};
        std::size_t count = 0u;
        for (std::size_t index = 4u; index < length && count < slstm32::CommandConsole::lineCapacity; ++index) {
            if (data[index] >= 0x20u && data[index] < 0x7Fu) line[count++] = static_cast<char>(data[index]);
        }
        // A console line shows its output: the stream runs for at least 30 s
        // more (an empty line only keeps it alive).
        startStream(id.source, data[3], consoleSessionMs);
        if (count != 0u) config_.console->execute(line);
        return;
    }
    case b1s::Service::logStream: {
        if (!config_.log) return;
        // Minutes: 1..60 start or extend, 0 stop, 255 only ask.
        const auto minutes = length > 4u ? data[4] : 0xFFu;
        if (minutes == 0u) {
            if (streamRequester_ == id.source) stopStream();
        } else if (minutes != 0xFFu) {
            startStream(id.source, data[3], (minutes < maxStreamMinutes ? minutes : maxStreamMinutes) * 60000u);
        }
        // The reply: status, remaining seconds (uint16; 0 = off).
        std::uint8_t reply[7]{};
        putHeader(reply);
        reply[2] = static_cast<std::uint8_t>(data[2] | replyBit);
        reply[3] = data[3];
        reply[4] = static_cast<std::uint8_t>(ParameterStatus::done);
        const auto remaining = streamRequester_ == id.source ? streamRemainingMs(runtime_.millis()) / 1000u : 0u;
        n2k::bytes::put16(reply + 5, static_cast<std::uint16_t>(remaining));
        node_.sendFast(servicePriority, pgn::service, id.source, reply, sizeof reply);
        return;
    }
    default:
        return;
    }
    pending_ = request;
    sendReply();
}

bool ServiceServer::sendReply() {
    if (!pending_.active || node_.fastPacketBusy()) return false;
    std::uint8_t reply[9u + 2u + maxTextLength]{};
    putHeader(reply);
    reply[2] = static_cast<std::uint8_t>(pending_.service | replyBit);
    reply[3] = pending_.sequence;
    std::size_t length = 5u;
    if (pending_.service == static_cast<std::uint8_t>(b1s::Service::parameterList)) {
        std::uint16_t ids[48];
        const auto total = parameterIds(ids, sizeof ids / sizeof ids[0]);
        const std::size_t first = pending_.first < total ? pending_.first : total;
        const std::size_t count = total - first < maxListed ? total - first : maxListed;
        reply[4] = static_cast<std::uint8_t>(ParameterStatus::done);
        reply[5] = static_cast<std::uint8_t>(first);
        reply[6] = static_cast<std::uint8_t>(count);
        reply[7] = first + count < total ? 1u : 0u;
        length = 8u;
        std::uint8_t list[8u + 2u * maxListed];
        for (std::size_t index = 0u; index < 8u; ++index) list[index] = reply[index];
        for (std::size_t index = 0u; index < count; ++index) {
            n2k::bytes::put16(list + length, ids[first + index]);
            length += 2u;
        }
        if (!node_.sendFast(servicePriority, pgn::service, pending_.requester, list, length)) return false;
        pending_.active = false;
        return true;
    }
    // Read and write replies carry the (stored) value.
    Value value{};
    auto status = pending_.status;
    if (status == ParameterStatus::done) status = read(static_cast<Parameter>(pending_.parameter), pending_.channel, value);
    reply[4] = static_cast<std::uint8_t>(status);
    n2k::bytes::put16(reply + 5, pending_.parameter);
    reply[7] = pending_.channel;
    length = 8u;
    if (status == ParameterStatus::done) {
        reply[8] = value.text ? typeText : typeNumber;
        length = 9u;
        if (value.text) {
            length += putStringLau(reply + length, value.string);
        } else {
            n2k::bytes::put32(reply + length, static_cast<std::uint32_t>(value.number));
            length += 4u;
        }
    }
    if (!node_.sendFast(servicePriority, pgn::service, pending_.requester, reply, length)) return false;
    pending_.active = false;
    return true;
}

void ServiceServer::handleCommand(const n2k::CanId& id, const std::uint8_t* data, std::uint8_t length) {
    if (length < 8u || (data[2] & replyBit) != 0u) return;
    const auto argument = static_cast<std::int32_t>(data[4] | data[5] << 8u | data[6] << 16u |
                                                    static_cast<std::uint32_t>(data[7]) << 24u);
    const auto result = runCommand(static_cast<Command>(data[2]), data[3], argument);
    putHeader(commandReply_);
    commandReply_[2] = static_cast<std::uint8_t>(data[2] | replyBit);
    commandReply_[3] = data[3];
    commandReply_[4] = static_cast<std::uint8_t>(result);
    commandReply_[5] = commandReply_[6] = commandReply_[7] = 0xFFu;
    commandReplyTo_ = id.source;
    if (node_.sendSingle(commandPriority, pgn::command, commandReplyTo_, commandReply_, sizeof commandReply_)) {
        commandReplyTo_ = n2k::nullAddress;
    }
}

CommandResult ServiceServer::runCommand(Command command, std::uint8_t channel, std::int32_t argument) {
    auto* measurements = config_.measurements;
    switch (command) {
    case Command::calibrate:
    case Command::calibrateSecondPoint:
    case Command::resetChannel: {
        const auto* config = config_.publisher->find(channel);
        if (!config || !measurements) return CommandResult::unknownChannel;
        const auto index = measurementIndex(*config);
        if (index >= measurements->size()) return CommandResult::unknownChannel;
        if (command == Command::resetChannel) {
            measurements->resetSettings(index);
            return CommandResult::done;
        }
        const auto reference = fromRaw(config->measurement->quantity(), argument);
        switch (measurements->calibrate(index, reference, command == Command::calibrateSecondPoint)) {
        case Result::stored:
        case Result::notStored: return CommandResult::done;
        case Result::implausibleGain: return CommandResult::invalidArgument;
        case Result::noReading:
        case Result::noFirstPoint: return CommandResult::refused;
        }
        return CommandResult::refused;
    }
    case Command::acknowledgeAlarms:
        if (!measurements) return CommandResult::unknownCommand;
        measurements->acknowledgeAll();
        return CommandResult::done;
    default:
        return config_.commands ? config_.commands(config_.commandContext, command, channel, argument)
                                : CommandResult::unknownCommand;
    }
}

const DeviceParameter* ServiceServer::deviceParameter(Parameter parameter) const {
    for (std::size_t index = 0u; index < config_.deviceParameterCount; ++index) {
        if (config_.deviceParameters[index].id == parameter) return &config_.deviceParameters[index];
    }
    return nullptr;
}

std::size_t ServiceServer::measurementIndex(const ChannelConfig& channel) const {
    return config_.measurements ? config_.measurements->find(channel.measurement->name()) : 0u;
}

std::size_t ServiceServer::parameterIds(std::uint16_t* ids, std::size_t capacity) const {
    std::size_t count = 0u;
    const auto add = [&](Parameter parameter) {
        if (count < capacity) ids[count++] = static_cast<std::uint16_t>(parameter);
    };
    if (config_.publisher->settings()) {
        for (const auto parameter : deviceTextParameters) add(parameter);
    }
    for (std::size_t index = 0u; index < config_.deviceParameterCount; ++index) add(config_.deviceParameters[index].id);
    add(Parameter::uptime);
    for (const auto parameter : channelParameters) add(parameter);
    if (config_.measurements) {
        for (const auto parameter : measurementParameters) add(parameter);
    }
    return count;
}

ParameterStatus ServiceServer::read(Parameter parameter, std::uint8_t channel, Value& value) {
    auto* settings = config_.publisher->settings();
    if (!isChannelParameter(parameter)) {
        if (parameter == Parameter::deviceRole && settings) {
            value.text = true;
            settings->readText(ChannelSettings::Text::role, 0u, value.string);
            return ParameterStatus::done;
        }
        if (parameter == Parameter::uptime) {
            value.number = static_cast<std::int32_t>(runtime_.millis() / 1000u);
            return ParameterStatus::done;
        }
        const auto* device = deviceParameter(parameter);
        if (!device || !device->read) return ParameterStatus::unknownParameter;
        return device->read(device->context, value.number) ? ParameterStatus::done : ParameterStatus::busy;
    }
    const auto* config = config_.publisher->find(channel);
    if (!config) return ParameterStatus::unknownChannel;
    const auto quantity = config->measurement->quantity();
    switch (parameter) {
    case Parameter::channelLocation:
    case Parameter::channelLabel: {
        value.text = true;
        const auto which = parameter == Parameter::channelLocation ? ChannelSettings::Text::location
                                                                    : ChannelSettings::Text::label;
        if (!settings || !settings->readText(which, channel, value.string)) {
            const char* text = which == ChannelSettings::Text::location ? config->location : config->label;
            for (std::size_t index = 0u; text && text[index] != '\0' && index < maxTextLength; ++index) {
                value.string[index] = text[index];
            }
        }
        return ParameterStatus::done;
    }
    case Parameter::channelEncoding:
        value.number = static_cast<std::int32_t>(config_.publisher->publish(*config));
        return ParameterStatus::done;
    case Parameter::channelStandardInstance:
        value.number = config_.publisher->standardInstance(*config);
        return ParameterStatus::done;
    case Parameter::channelStandardSource:
        value.number = config_.publisher->standardSource(*config);
        return ParameterStatus::done;
    default:
        break;
    }
    if (!config_.measurements) return ParameterStatus::unknownParameter;
    const auto& measurement = *config->measurement;
    switch (parameter) {
    case Parameter::calibrationOffset:
        value.number = toRawDelta(quantity, measurement.calibration().offset);
        return ParameterStatus::done;
    case Parameter::calibrationGain:
        value.number = measurement.calibration().gainPpm;
        return ParameterStatus::done;
    case Parameter::alarmThreshold0:
    case Parameter::alarmThreshold1:
    case Parameter::alarmThreshold2:
    case Parameter::alarmThreshold3: {
        const auto rule = ruleOf(parameter, Parameter::alarmThreshold0);
        if (rule >= measurement.alarmCount()) return ParameterStatus::unknownParameter;
        const auto& alarm = measurement.alarmRule(rule);
        value.number = absoluteRule(alarm.kind) ? toRaw(quantity, alarm.threshold) : toRawDelta(quantity, alarm.threshold);
        return ParameterStatus::done;
    }
    case Parameter::alarmRule0:
    case Parameter::alarmRule1:
    case Parameter::alarmRule2:
    case Parameter::alarmRule3: {
        const auto rule = ruleOf(parameter, Parameter::alarmRule0);
        if (rule >= measurement.alarmCount()) return ParameterStatus::unknownParameter;
        const auto& alarm = measurement.alarmRule(rule);
        value.number = static_cast<std::int32_t>(static_cast<std::uint32_t>(alarm.kind) |
                                                 static_cast<std::uint32_t>(alarm.severity) << 8u |
                                                 (alarm.latching ? 1u << 16u : 0u) |
                                                 (measurement.alarmActive(rule) ? 1u << 17u : 0u) |
                                                 (measurement.alarmAcknowledged(rule) ? 1u << 18u : 0u));
        return ParameterStatus::done;
    }
    default:
        return ParameterStatus::unknownParameter;
    }
}

ParameterStatus ServiceServer::write(Parameter parameter, std::uint8_t channel, const Value& value) {
    auto* settings = config_.publisher->settings();
    const bool textParameter = parameter == Parameter::deviceRole || parameter == Parameter::channelLocation ||
                               parameter == Parameter::channelLabel;
    if (textParameter != value.text) return ParameterStatus::invalidValue;
    if (!isChannelParameter(parameter)) {
        if (parameter == Parameter::deviceRole && settings) {
            return settings->writeText(ChannelSettings::Text::role, 0u, value.string) ? ParameterStatus::done
                                                                                      : ParameterStatus::busy;
        }
        if (parameter == Parameter::uptime) return ParameterStatus::readOnly;
        const auto* device = deviceParameter(parameter);
        if (!device) return ParameterStatus::unknownParameter;
        return device->write ? device->write(device->context, value.number) : ParameterStatus::readOnly;
    }
    const auto* config = config_.publisher->find(channel);
    if (!config) return ParameterStatus::unknownChannel;
    switch (parameter) {
    case Parameter::channelLocation:
    case Parameter::channelLabel: {
        if (!settings) return ParameterStatus::readOnly;
        // Locations are Signal K path fragments: letters, digits, dots.
        if (parameter == Parameter::channelLocation) {
            for (const char* cursor = value.string; *cursor != '\0'; ++cursor) {
                const char c = *cursor;
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.')) {
                    return ParameterStatus::invalidValue;
                }
            }
        }
        const auto which = parameter == Parameter::channelLocation ? ChannelSettings::Text::location
                                                                    : ChannelSettings::Text::label;
        if (!settings->writeText(which, channel, value.string)) return ParameterStatus::busy;
        config_.publisher->describe(*config);
        return ParameterStatus::done;
    }
    case Parameter::channelEncoding:
    case Parameter::channelStandardInstance:
    case Parameter::channelStandardSource: {
        if (!settings || !settings->overrideOf(channel)) return ParameterStatus::readOnly;
        auto stored = *settings->overrideOf(channel);
        if (parameter == Parameter::channelEncoding) {
            if (value.number < 0 || value.number > static_cast<std::int32_t>(Publish::off)) return ParameterStatus::invalidValue;
            stored.publish = static_cast<std::uint8_t>(value.number);
        } else {
            if (value.number < 0 || (value.number > 252 && value.number != 255)) return ParameterStatus::invalidValue;
            (parameter == Parameter::channelStandardInstance ? stored.standardInstance : stored.standardSource) =
                static_cast<std::uint8_t>(value.number);
        }
        if (!settings->writeOverride(channel, stored)) return ParameterStatus::busy;
        config_.publisher->describe(*config);
        return ParameterStatus::done;
    }
    default:
        break;
    }
    auto* measurements = config_.measurements;
    if (!measurements) return ParameterStatus::unknownParameter;
    const auto index = measurementIndex(*config);
    if (index >= measurements->size()) return ParameterStatus::unknownChannel;
    const auto& measurement = *config->measurement;
    const auto quantity = measurement.quantity();
    switch (parameter) {
    case Parameter::calibrationOffset:
    case Parameter::calibrationGain: {
        auto calibration = measurement.calibration();
        if (parameter == Parameter::calibrationOffset) {
            calibration.offset = fromRawDelta(quantity, value.number);
        } else {
            if (value.number < 500000 || value.number > 2000000) return ParameterStatus::invalidValue;
            calibration.gainPpm = value.number;
        }
        measurements->setCalibration(index, calibration);
        return ParameterStatus::done;
    }
    case Parameter::alarmThreshold0:
    case Parameter::alarmThreshold1:
    case Parameter::alarmThreshold2:
    case Parameter::alarmThreshold3: {
        const auto rule = ruleOf(parameter, Parameter::alarmThreshold0);
        if (rule >= measurement.alarmCount()) return ParameterStatus::unknownParameter;
        const auto kind = measurement.alarmRule(rule).kind;
        if (kind == AlarmKind::sensorFault) return ParameterStatus::notPermitted;
        const auto threshold = absoluteRule(kind) ? fromRaw(quantity, value.number) : fromRawDelta(quantity, value.number);
        measurements->setThreshold(index, rule, threshold);
        return ParameterStatus::done;
    }
    case Parameter::alarmRule0:
    case Parameter::alarmRule1:
    case Parameter::alarmRule2:
    case Parameter::alarmRule3:
        return ParameterStatus::readOnly;
    default:
        return ParameterStatus::unknownParameter;
    }
}

void ServiceServer::startStream(std::uint8_t requester, std::uint8_t sequence, std::uint32_t durationMs) {
    const auto now = runtime_.millis();
    if (streamRequester_ != requester || !config_.log->mirroring()) {
        config_.log->startMirror(); // from now on; earlier output is not sent
        streamLost_ = config_.log->mirrorLost();
        streamBudget_ = streamBurstBytes;
        streamUntil_ = now;
    }
    streamRequester_ = requester;
    streamSequence_ = sequence;
    // Extend, never shorten: a console line does not cut a longer stream.
    if (streamRemainingMs(now) < durationMs) streamUntil_ = now + durationMs;
}

void ServiceServer::stopStream() {
    config_.log->stopMirror();
    streamRequester_ = n2k::nullAddress;
}

std::uint32_t ServiceServer::streamRemainingMs(std::uint32_t now) const {
    if (streamRequester_ == n2k::nullAddress || reached(now, streamUntil_)) return 0u;
    return streamUntil_ - now;
}

void ServiceServer::streamLog(std::uint32_t now) {
    if (streamRequester_ == n2k::nullAddress || !config_.log) return;
    if (reached(now, streamUntil_)) {
        stopStream(); // the time is up: the device stops by itself
        return;
    }
    if (!reached(now, streamSentAt_ + streamIntervalMs) || node_.fastPacketBusy() || !node_.claimed()) return;
    // Token bucket: the budget refills at 512 B/s up to one chunk and goes
    // negative when a drop marker overdraws it, so the average holds.
    streamBudget_ += static_cast<std::int32_t>(streamBytesPerInterval * ((now - streamSentAt_) / streamIntervalMs));
    if (streamBudget_ > static_cast<std::int32_t>(streamBurstBytes)) streamBudget_ = streamBurstBytes;
    streamSentAt_ = now;
    if (streamBudget_ <= 0) return;
    std::uint8_t reply[5u + streamChunk];
    std::size_t count = 0u;
    if (config_.log->mirrorLost() != streamLost_) {
        streamLost_ = config_.log->mirrorLost();
        for (; droppedMarker[count] != '\0'; ++count) reply[5u + count] = static_cast<std::uint8_t>(droppedMarker[count]);
    }
    const auto budget = static_cast<std::size_t>(streamBudget_);
    const auto room = count < budget ? (budget - count < streamChunk - count ? budget - count : streamChunk - count) : 0u;
    count += config_.log->readMirror(reply + 5u + count, room);
    if (count == 0u) return;
    streamBudget_ -= static_cast<std::int32_t>(count);
    putHeader(reply);
    reply[2] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(b1s::Service::console) | replyBit);
    reply[3] = streamSequence_;
    reply[4] = static_cast<std::uint8_t>(ParameterStatus::done);
    node_.sendFast(servicePriority, pgn::service, streamRequester_, reply, 5u + count);
}

void ServiceServer::run() {
    if (commandReplyTo_ != n2k::nullAddress &&
        node_.sendSingle(commandPriority, pgn::command, commandReplyTo_, commandReply_, sizeof commandReply_)) {
        commandReplyTo_ = n2k::nullAddress;
    }
    if (pending_.active) {
        sendReply();
        return;
    }
    streamLog(runtime_.millis());
}

} // namespace b1s
