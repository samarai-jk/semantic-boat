#include "can_test_service.hpp"
#include <cstdio>

namespace lab_test_01 {
namespace {

using slstm32::LogLevel;
using slstm32::drivers::CanState;

constexpr std::uint32_t testFrameId = 0x18FF0042u;
constexpr std::uint32_t sendPeriodMs = 1000u;
constexpr std::uint32_t summaryPeriodMs = 10000u;
constexpr std::uint32_t frameLogWindowMs = 1000u;
constexpr std::uint8_t framesLoggedPerWindow = 10u;
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

bool CanTestService::init() {
    const auto now = runtime_.millis();
    nextSendAt_ = now + sendPeriodMs;
    nextSummaryAt_ = now + summaryPeriodMs;
    logWindowStartedAt_ = now;
    lastStatus_ = can_.status();
    log_.writef(LogLevel::info, "can: %s, test frame 0x%08lX every %lu ms",
                stateName(lastStatus_.state), static_cast<unsigned long>(testFrameId),
                static_cast<unsigned long>(sendPeriodMs));
    return true;
}

void CanTestService::run() {
    const auto now = runtime_.millis();
    sendTestFrame(now);
    drainReceived(now);
    watchStatus();
    showActivity();
    summary(now);
}

void CanTestService::sendTestFrame(std::uint32_t now) {
    if (!due(now, nextSendAt_)) return;
    nextSendAt_ += sendPeriodMs;
    const auto seconds = static_cast<std::uint16_t>(now / 1000u);
    slstm32::drivers::CanFrame frame{};
    frame.id = testFrameId;
    frame.extended = true;
    frame.length = 8u;
    frame.data = {static_cast<std::uint8_t>(sequence_), static_cast<std::uint8_t>(sequence_ >> 8u),
                  static_cast<std::uint8_t>(sequence_ >> 16u),
                  static_cast<std::uint8_t>(sequence_ >> 24u), static_cast<std::uint8_t>(seconds),
                  static_cast<std::uint8_t>(seconds >> 8u), 0xCAu, 0xFEu};
    ++sequence_;
    (void)can_.send(frame); // A full queue is counted by the driver.
}

void CanTestService::drainReceived(std::uint32_t now) {
    if (due(now, logWindowStartedAt_ + frameLogWindowMs)) {
        if (suppressedFrames_ != 0u) {
            log_.writef(LogLevel::info, "can rx: %lu more frame(s) not logged",
                        static_cast<unsigned long>(suppressedFrames_));
        }
        logWindowStartedAt_ = now;
        loggedInWindow_ = 0u;
        suppressedFrames_ = 0u;
    }
    slstm32::drivers::CanFrame frame{};
    while (can_.receive(frame)) {
        activityLed_.pulse(activityPulseMs);
        if (loggedInWindow_ < framesLoggedPerWindow) {
            ++loggedInWindow_;
            logFrame(frame);
        } else {
            ++suppressedFrames_;
        }
    }
}

void CanTestService::watchStatus() {
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

void CanTestService::showActivity() {
    const auto transmitted = can_.counters().transmitted;
    if (transmitted == lastTransmitted_) return;
    lastTransmitted_ = transmitted;
    activityLed_.pulse(activityPulseMs);
}

void CanTestService::summary(std::uint32_t now) {
    if (!due(now, nextSummaryAt_)) return;
    nextSummaryAt_ += summaryPeriodMs;
    const auto counters = can_.counters();
    const auto status = can_.status();
    log_.writef(LogLevel::debug,
                "can: rx %lu, tx %lu, tx drop %lu, rx drop %lu, ovr %lu, %s TEC %u REC %u",
                static_cast<unsigned long>(counters.received),
                static_cast<unsigned long>(counters.transmitted),
                static_cast<unsigned long>(counters.transmitDropped),
                static_cast<unsigned long>(counters.receiveDropped),
                static_cast<unsigned long>(counters.hardwareOverruns), stateName(status.state),
                static_cast<unsigned>(status.transmitErrors),
                static_cast<unsigned>(status.receiveErrors));
}

void CanTestService::logFrame(const slstm32::drivers::CanFrame& frame) const {
    char bytes[3u * 8u + 1u]{};
    std::size_t used = 0u;
    for (std::uint8_t index = 0u; index < frame.length && index < 8u; ++index) {
        used += static_cast<std::size_t>(std::snprintf(bytes + used, sizeof bytes - used, " %02X",
                                                       static_cast<unsigned>(frame.data[index])));
    }
    log_.writef(LogLevel::info, "can rx %s 0x%0*lX%s [%u]%s", frame.extended ? "ext" : "std",
                frame.extended ? 8 : 3, static_cast<unsigned long>(frame.id),
                frame.remote ? " RTR" : "", static_cast<unsigned>(frame.length), bytes);
}

} // namespace lab_test_01
