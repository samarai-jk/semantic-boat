#include "n2k/node.hpp"
#include <cstring>
#include "n2k/bytes.hpp"
#include "n2k/pgn.hpp"

namespace n2k {
namespace {

using slstm32::LogLevel;
using slstm32::drivers::CanFrame;
using slstm32::drivers::CanState;

constexpr std::uint32_t claimSettleMs = 250u;
constexpr std::uint32_t fastPacketStallMs = 1000u;
constexpr std::uint8_t networkPriority = 6u;
constexpr std::uint8_t heartbeatPriority = 7u;
constexpr std::uint8_t heartbeatSequenceLimit = 253u;
constexpr std::size_t productInfoLength = 134u;
constexpr std::size_t productStringLength = 32u;

constexpr std::uint32_t stackTransmitPgns[]{
    pgn::isoAcknowledgement, pgn::isoAddressClaim, pgn::pgnList, pgn::heartbeat,
    pgn::productInformation,
};
constexpr std::uint32_t stackReceivePgns[]{
    pgn::isoAcknowledgement, pgn::isoRequest, pgn::isoAddressClaim,
};

bool reached(std::uint32_t now, std::uint32_t deadline) {
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

} // namespace

bool Node::init() {
    if (!runtime_.millis) return false;
    address_ = config_.preferredAddress <= maximumAddress ? config_.preferredAddress : 0u;
    reportedAddress_ = config_.preferredAddress;
    startClaim(runtime_.millis());
    return true;
}

void Node::run() {
    const auto now = runtime_.millis();
    CanFrame frame{};
    while (can_.receive(frame)) handleFrame(frame, now);

    if (state_ == State::claiming && reached(now, claimStartedAt_ + claimSettleMs)) {
        state_ = State::claimed;
        nextHeartbeatAt_ = now;
        log_.writef(LogLevel::info, "n2k: claimed address %u", static_cast<unsigned>(address_));
        if (address_ != reportedAddress_) {
            reportedAddress_ = address_;
            if (addressHandler_) addressHandler_(addressContext_, address_);
        }
    }
    if (claimed() && config_.heartbeatIntervalMs != 0u && reached(now, nextHeartbeatAt_)) {
        nextHeartbeatAt_ = now + config_.heartbeatIntervalMs;
        pending_ |= pendingHeartbeat;
    }
    servePending();
    pumpFastPacket(now);
}

void Node::startClaim(std::uint32_t now) {
    state_ = State::claiming;
    claimStartedAt_ = now;
    pending_ |= pendingClaim;
    // Abandon a fast packet sent from the previous address.
    fastLength_ = 0u;
}

void Node::handleFrame(const CanFrame& frame, std::uint32_t now) {
    if (!frame.extended || frame.remote) return;
    const auto id = decodeCanId(frame.id);
    if (id.pgn == pgn::isoAddressClaim) {
        handleAddressClaim(id, frame.data.data(), frame.length, now);
        return;
    }
    if (id.source <= maximumAddress && id.source != address_) markUsed(id.source);
    if (id.destination != broadcastAddress && id.destination != address_) return;
    if (id.pgn == pgn::isoRequest) {
        handleRequest(id, frame.data.data(), frame.length);
    } else if (messageHandler_) {
        messageHandler_(messageContext_, id, frame.data.data(), frame.length);
    }
}

void Node::handleAddressClaim(const CanId& id, const std::uint8_t* data, std::uint8_t length,
                              std::uint32_t now) {
    if (length < 8u || id.source > maximumAddress) return;
    if (id.source != address_ || state_ == State::idle || state_ == State::cannotClaim) {
        markUsed(id.source);
        return;
    }
    const auto theirs = bytes::get64(data);
    const auto ours = config_.name.value();
    if (ours < theirs) {
        ++counters_.conflictsWon;
        pending_ |= pendingClaim;
        log_.writef(LogLevel::warning, "n2k: address %u contested, our NAME wins",
                    static_cast<unsigned>(address_));
        return;
    }
    ++counters_.conflictsLost;
    markUsed(address_);
    std::uint8_t next{};
    if (nextFreeAddress(next)) {
        log_.writef(LogLevel::warning, "n2k: address %u lost to a lower NAME, trying %u",
                    static_cast<unsigned>(address_), static_cast<unsigned>(next));
        address_ = next;
        startClaim(now);
    } else {
        log_.write(LogLevel::error, "n2k: no free address, cannot claim");
        state_ = State::cannotClaim;
        address_ = nullAddress;
        fastLength_ = 0u;
        pending_ = pendingClaim;
    }
}

void Node::handleRequest(const CanId& id, const std::uint8_t* data, std::uint8_t length) {
    if (length < 3u) return;
    const auto requested = bytes::get24(data);
    if (requested == pgn::isoAddressClaim) {
        if (state_ != State::idle) pending_ |= pendingClaim;
        ++counters_.requestsAnswered;
        return;
    }
    if (!claimed()) return;
    switch (requested) {
    case pgn::productInformation:
        pending_ |= pendingProductInfo;
        break;
    case pgn::pgnList:
        pending_ |= pendingTransmitList | pendingReceiveList;
        listDestination_ = id.source <= maximumAddress ? id.source : broadcastAddress;
        break;
    case pgn::heartbeat:
        pending_ |= pendingHeartbeat;
        break;
    default:
        // Only a request addressed to this node expects an answer.
        if (id.destination == address_ && id.source <= maximumAddress) {
            nackDestination_ = id.source;
            nackPgn_ = requested;
            pending_ |= pendingNack;
            ++counters_.requestsRejected;
            log_.writef(LogLevel::debug, "n2k: NACK request for PGN %lu from %u",
                        static_cast<unsigned long>(requested), static_cast<unsigned>(id.source));
        }
        return;
    }
    ++counters_.requestsAnswered;
    log_.writef(LogLevel::debug, "n2k: request for PGN %lu from %u",
                static_cast<unsigned long>(requested), static_cast<unsigned>(id.source));
}

void Node::servePending() {
    if ((pending_ & pendingClaim) && sendClaim()) pending_ &= static_cast<std::uint8_t>(~pendingClaim);
    if (!claimed()) return;
    if ((pending_ & pendingHeartbeat) && sendHeartbeat()) {
        pending_ &= static_cast<std::uint8_t>(~pendingHeartbeat);
    }
    if ((pending_ & pendingNack) && sendNack()) pending_ &= static_cast<std::uint8_t>(~pendingNack);
    if (fastPacketBusy()) return;
    if (pending_ & pendingProductInfo) {
        if (beginProductInfo()) pending_ &= static_cast<std::uint8_t>(~pendingProductInfo);
    } else if (pending_ & pendingTransmitList) {
        if (beginPgnList(0u)) pending_ &= static_cast<std::uint8_t>(~pendingTransmitList);
    } else if (pending_ & pendingReceiveList) {
        if (beginPgnList(1u)) pending_ &= static_cast<std::uint8_t>(~pendingReceiveList);
    }
}

bool Node::sendClaim() {
    std::uint8_t data[8];
    bytes::put64(data, config_.name.value());
    CanId id{};
    id.priority = networkPriority;
    id.pgn = pgn::isoAddressClaim;
    id.source = state_ == State::cannotClaim ? nullAddress : address_;
    id.destination = broadcastAddress;
    return sendFrame(id, data, sizeof data);
}

bool Node::sendHeartbeat() {
    std::uint8_t controller = 0u; // error-active
    switch (can_.status().state) {
    case CanState::errorPassive: controller = 1u; break;
    case CanState::busOff: controller = 2u; break;
    default: break;
    }
    std::uint8_t data[8];
    // Interval in 0.01 s, sequence counter, controller 1 state, controller 2
    // not available, equipment operational, reserved bits set.
    bytes::put16(data, static_cast<std::uint16_t>(config_.heartbeatIntervalMs / 10u));
    data[2] = heartbeatSequence_;
    data[3] = static_cast<std::uint8_t>(controller | (3u << 2u) | (0u << 4u) | (3u << 6u));
    data[4] = data[5] = data[6] = data[7] = 0xFFu;
    CanId id{};
    id.priority = heartbeatPriority;
    id.pgn = pgn::heartbeat;
    id.source = address_;
    if (!sendFrame(id, data, sizeof data)) return false;
    heartbeatSequence_ = static_cast<std::uint8_t>((heartbeatSequence_ + 1u) % heartbeatSequenceLimit);
    return true;
}

bool Node::sendNack() {
    std::uint8_t data[8]{1u, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
    bytes::put24(data + 5, nackPgn_);
    CanId id{};
    id.priority = networkPriority;
    id.pgn = pgn::isoAcknowledgement;
    id.source = address_;
    id.destination = nackDestination_;
    return sendFrame(id, data, sizeof data);
}

bool Node::beginProductInfo() {
    const auto& product = config_.product;
    auto* out = fastData_;
    bytes::put16(out, product.n2kVersion);
    bytes::put16(out + 2, product.productCode);
    bytes::putString(out + 4, productStringLength, product.modelId);
    bytes::putString(out + 36, productStringLength, product.softwareVersion);
    bytes::putString(out + 68, productStringLength, product.modelVersion);
    bytes::putString(out + 100, productStringLength, product.serialCode);
    out[132] = product.certificationLevel;
    out[133] = product.loadEquivalency;
    return beginFast(networkPriority, pgn::productInformation, broadcastAddress, productInfoLength);
}

bool Node::beginPgnList(std::uint8_t function) {
    const bool transmit = function == 0u;
    const auto* stack = transmit ? stackTransmitPgns : stackReceivePgns;
    const std::size_t stackCount = transmit ? sizeof stackTransmitPgns / sizeof stackTransmitPgns[0]
                                            : sizeof stackReceivePgns / sizeof stackReceivePgns[0];
    const auto* application = transmit ? config_.transmitPgns : config_.receivePgns;
    const auto applicationCount = transmit ? config_.transmitPgnCount : config_.receivePgnCount;
    std::size_t length = 0u;
    fastData_[length++] = function;
    for (std::size_t index = 0u; index < stackCount + applicationCount; ++index) {
        if (length + 3u > fastPacketCapacity) break;
        const auto value = index < stackCount ? stack[index] : application[index - stackCount];
        bytes::put24(fastData_ + length, value);
        length += 3u;
    }
    return beginFast(networkPriority, pgn::pgnList, listDestination_, length);
}

bool Node::sendSingle(std::uint8_t priority, std::uint32_t pgn, std::uint8_t destination,
                      const std::uint8_t* data, std::uint8_t length) {
    if (!claimed() || length > 8u) return false;
    CanId id{};
    id.priority = priority;
    id.pgn = pgn;
    id.source = address_;
    id.destination = destination;
    return sendFrame(id, data, length);
}

bool Node::sendFast(std::uint8_t priority, std::uint32_t pgn, std::uint8_t destination,
                    const std::uint8_t* data, std::size_t length) {
    if (!claimed() || fastPacketBusy() || length > fastPacketCapacity) return false;
    std::memcpy(fastData_, data, length);
    return beginFast(priority, pgn, destination, length);
}

bool Node::beginFast(std::uint8_t priority, std::uint32_t pgn, std::uint8_t destination,
                     std::size_t length) {
    if (length == 0u || length > fastPacketCapacity) return false;
    fastId_ = {priority, pgn, address_, destination};
    fastLength_ = length;
    fastOffset_ = 0u;
    fastFrame_ = 0u;
    fastSequence_ = static_cast<std::uint8_t>((fastSequence_ + 1u) & 0x7u);
    fastProgressAt_ = runtime_.millis();
    return true;
}

void Node::pumpFastPacket(std::uint32_t now) {
    if (!fastPacketBusy()) return;
    if (!claimed()) {
        fastLength_ = 0u;
        return;
    }
    while (fastOffset_ < fastLength_ && can_.transmitSpace() != 0u) {
        std::uint8_t data[8];
        std::memset(data, 0xFF, sizeof data);
        data[0] = static_cast<std::uint8_t>(fastSequence_ << 5u | fastFrame_);
        std::size_t index = 1u;
        if (fastFrame_ == 0u) data[index++] = static_cast<std::uint8_t>(fastLength_);
        while (index < 8u && fastOffset_ < fastLength_) data[index++] = fastData_[fastOffset_++];
        if (!sendFrame(fastId_, data, 8u)) return;
        ++fastFrame_;
        fastProgressAt_ = now;
    }
    if (fastOffset_ >= fastLength_) {
        fastLength_ = 0u;
    } else if (reached(now, fastProgressAt_ + fastPacketStallMs)) {
        ++counters_.fastPacketsAborted;
        log_.writef(LogLevel::warning, "n2k: fast packet PGN %lu aborted, bus not accepting frames",
                    static_cast<unsigned long>(fastId_.pgn));
        fastLength_ = 0u;
    }
}

bool Node::sendFrame(const CanId& id, const std::uint8_t* data, std::uint8_t length) {
    // Pending messages retry on every run(); only a frame that cannot be
    // queued although space was reported counts as not sent.
    if (can_.transmitSpace() == 0u) return false;
    CanFrame frame{};
    frame.id = encodeCanId(id);
    frame.extended = true;
    frame.length = length;
    std::memcpy(frame.data.data(), data, length);
    if (can_.send(frame)) return true;
    ++counters_.framesNotSent;
    return false;
}

bool Node::nextFreeAddress(std::uint8_t& address) const {
    for (std::uint16_t step = 1u; step <= maximumAddress + 1u; ++step) {
        const auto candidate = static_cast<std::uint8_t>((address_ + step) % (maximumAddress + 1u));
        if (!isUsed(candidate)) {
            address = candidate;
            return true;
        }
    }
    return false;
}

void Node::markUsed(std::uint8_t address) {
    usedAddresses_[address / 32u] |= 1u << (address % 32u);
}

bool Node::isUsed(std::uint8_t address) const {
    return (usedAddresses_[address / 32u] & (1u << (address % 32u))) != 0u;
}

} // namespace n2k
