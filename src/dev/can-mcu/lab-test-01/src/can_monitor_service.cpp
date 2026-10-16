#include "can_monitor_service.hpp"
#include <cstdio>

namespace lab_test_01 {
namespace {

using slstm32::LogLevel;
using slstm32::drivers::CanState;

constexpr std::uint32_t summaryPeriodMs = 10000u;
constexpr std::uint32_t messageLogWindowMs = 1000u;
constexpr std::uint8_t messagesLoggedPerWindow = 10u;
constexpr std::uint32_t activityPulseMs = 20u;

bool due(std::uint32_t now, std::uint32_t deadline) {
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

const char* stateName(CanState state) {
    switch (state) {
    case CanState::stopped: return "stopped";
    case CanState::errorActive: return "error-active";
    case CanState::errorWarning: return "error-warning";
    case CanState::errorPassive: return "error-passive";
    case CanState::busOff: return "bus-off";
    }
    return "?";
}

const char* nodeStateName(n2k::Node::State state) {
    switch (state) {
    case n2k::Node::State::idle: return "idle";
    case n2k::Node::State::claiming: return "claiming";
    case n2k::Node::State::claimed: return "claimed";
    case n2k::Node::State::cannotClaim: return "cannot-claim";
    }
    return "?";
}

// bxCAN last error code (ESR.LEC).
const char* errorName(std::uint8_t code) {
    switch (code) {
    case 0u: return "none";
    case 1u: return "stuff";
    case 2u: return "form";
    case 3u: return "ack";
    case 4u: return "bit-recessive";
    case 5u: return "bit-dominant";
    case 6u: return "crc";
    default: return "other";
    }
}

// Error counters are logged in steps of 32 so that a bus without any
// acknowledging node does not flood the log.
std::uint8_t bucket(std::uint8_t counter) {
    return static_cast<std::uint8_t>(counter / 32u);
}

} // namespace

bool CanMonitorService::init() {
    const auto now = runtime_.millis();
    nextSummaryAt_ = now + summaryPeriodMs;
    logWindowStartedAt_ = now;
    lastStatus_ = can_.status();
    log_.writef(LogLevel::info, "can: %s", stateName(lastStatus_.state));
    return true;
}

void CanMonitorService::run() {
    const auto now = runtime_.millis();
    if (due(now, logWindowStartedAt_ + messageLogWindowMs)) {
        if (suppressedMessages_ != 0u) {
            log_.writef(LogLevel::info, "n2k rx: %lu more message(s) not logged",
                        static_cast<unsigned long>(suppressedMessages_));
        }
        logWindowStartedAt_ = now;
        loggedInWindow_ = 0u;
        suppressedMessages_ = 0u;
    }
    watchStatus();
    showActivity();
    summary(now);
}

void CanMonitorService::onMessage(void* context, const n2k::CanId& id, const std::uint8_t* data,
                                  std::uint8_t length) {
    static_cast<CanMonitorService*>(context)->logMessage(id, data, length);
}

void CanMonitorService::logMessage(const n2k::CanId& id, const std::uint8_t* data,
                                   std::uint8_t length) {
    if (loggedInWindow_ >= messagesLoggedPerWindow) {
        ++suppressedMessages_;
        return;
    }
    ++loggedInWindow_;
    char bytes[3u * 8u + 1u]{};
    std::size_t used = 0u;
    for (std::uint8_t index = 0u; index < length && index < 8u; ++index) {
        used += static_cast<std::size_t>(std::snprintf(bytes + used, sizeof bytes - used, " %02X",
                                                       static_cast<unsigned>(data[index])));
    }
    log_.writef(LogLevel::info, "n2k rx PGN %lu src %u dst %u prio %u [%u]%s",
                static_cast<unsigned long>(id.pgn), static_cast<unsigned>(id.source),
                static_cast<unsigned>(id.destination), static_cast<unsigned>(id.priority),
                static_cast<unsigned>(length), bytes);
}

void CanMonitorService::watchStatus() {
    const auto status = can_.status();
    if (status.state == lastStatus_.state &&
        bucket(status.transmitErrors) == bucket(lastStatus_.transmitErrors) &&
        bucket(status.receiveErrors) == bucket(lastStatus_.receiveErrors)) {
        return;
    }
    log_.writef(LogLevel::warning, "can state: %s, TEC %u, REC %u, last error %s",
                stateName(status.state), static_cast<unsigned>(status.transmitErrors),
                static_cast<unsigned>(status.receiveErrors), errorName(status.lastErrorCode));
    lastStatus_ = status;
}

void CanMonitorService::showActivity() {
    const auto counters = can_.counters();
    const auto traffic = counters.received + counters.transmitted;
    if (traffic == lastTraffic_) return;
    lastTraffic_ = traffic;
    activityLed_.pulse(activityPulseMs);
}

void CanMonitorService::summary(std::uint32_t now) {
    if (!due(now, nextSummaryAt_)) return;
    nextSummaryAt_ += summaryPeriodMs;
    const auto counters = can_.counters();
    const auto status = can_.status();
    const auto& n2kCounters = node_.counters();
    log_.writef(LogLevel::debug, "can: rx %lu, tx %lu, drop tx %lu rx %lu ovr %lu, %s TEC %u REC %u",
                static_cast<unsigned long>(counters.received),
                static_cast<unsigned long>(counters.transmitted),
                static_cast<unsigned long>(counters.transmitDropped),
                static_cast<unsigned long>(counters.receiveDropped),
                static_cast<unsigned long>(counters.hardwareOverruns), stateName(status.state),
                static_cast<unsigned>(status.transmitErrors),
                static_cast<unsigned>(status.receiveErrors));
    log_.writef(LogLevel::debug,
                "n2k: %s address %u, requests %lu answered %lu nacked, conflicts %lu won %lu lost",
                nodeStateName(node_.state()), static_cast<unsigned>(node_.address()),
                static_cast<unsigned long>(n2kCounters.requestsAnswered),
                static_cast<unsigned long>(n2kCounters.requestsRejected),
                static_cast<unsigned long>(n2kCounters.conflictsWon),
                static_cast<unsigned long>(n2kCounters.conflictsLost));
}

} // namespace lab_test_01
