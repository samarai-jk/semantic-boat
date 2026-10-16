#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "n2k/can_id.hpp"
#include "n2k/name.hpp"
#include "slstm32/drivers/can_bus.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace n2k {

// PGN 126996 content. Strings are truncated to 32 characters.
struct ProductInfo {
    std::uint16_t n2kVersion{2100u}; // 2.100
    std::uint16_t productCode{};
    const char* modelId{};
    const char* softwareVersion{};
    const char* modelVersion{};
    const char* serialCode{};
    std::uint8_t certificationLevel{};
    std::uint8_t loadEquivalency{1u}; // units of 50 mA from the bus
};

struct NodeConfig {
    Name name{};
    std::uint8_t preferredAddress{};
    ProductInfo product{};
    // Application PGNs reported in PGN 126464 in addition to the stack's own.
    const std::uint32_t* transmitPgns{};
    std::size_t transmitPgnCount{};
    const std::uint32_t* receivePgns{};
    std::size_t receivePgnCount{};
    std::uint32_t heartbeatIntervalMs{60000u};
};

// Single-frame message for the application: a frame addressed to this node
// or broadcast that the stack does not handle itself.
using MessageHandler = void (*)(void* context, const CanId& id, const std::uint8_t* data,
                                std::uint8_t length);
using AddressHandler = void (*)(void* context, std::uint8_t address);

// Minimal NMEA 2000 node on a CanBusDriver.
//
// - Address claim (PGN 60928) at the preferred address. A conflicting claim
//   is decided by NAME: the node re-asserts its claim when its NAME is lower
//   and otherwise moves to the next free address (or reports "cannot claim"
//   from address 254). The address handler reports a changed claimed address
//   so it can be persisted.
// - ISO requests (PGN 59904) for the address claim, product information
//   (126996), the PGN list (126464), and the heartbeat are answered; other
//   requests addressed to the node get an ISO acknowledgement NACK (59392).
// - Heartbeat (126993) at the configured interval with the CAN controller
//   state.
// - Fast-packet transmission from a single 223-byte buffer, paced by the free
//   space of the CAN transmit queue. Fast-packet reception is not supported.
//
// The node owns the receive side of the CAN driver: run() drains it and
// forwards unhandled single frames to the message handler.
class Node final : public slstm32::Service {
public:
    static constexpr std::size_t fastPacketCapacity = 223u;
    enum class State : std::uint8_t { idle, claiming, claimed, cannotClaim };

    struct Counters {
        std::uint32_t requestsAnswered{};
        std::uint32_t requestsRejected{};
        std::uint32_t conflictsWon{};
        std::uint32_t conflictsLost{};
        std::uint32_t framesNotSent{};
        std::uint32_t fastPacketsAborted{};
    };

    Node(slstm32::Runtime runtime, slstm32::drivers::CanBusDriver& can, const NodeConfig& config,
         slstm32::Logger log = {})
        : runtime_(runtime), can_(can), config_(config), log_(log) {}

    void onMessage(MessageHandler handler, void* context) {
        messageHandler_ = handler;
        messageContext_ = context;
    }
    void onAddressChange(AddressHandler handler, void* context) {
        addressHandler_ = handler;
        addressContext_ = context;
    }

    bool init() override;
    void run() override;

    State state() const { return state_; }
    bool claimed() const { return state_ == State::claimed; }
    std::uint8_t address() const { return address_; }
    const Counters& counters() const { return counters_; }

    // Requires a claimed address. Returns false if the transmit queue is full.
    bool sendSingle(std::uint8_t priority, std::uint32_t pgn, std::uint8_t destination,
                    const std::uint8_t* data, std::uint8_t length);
    // Copies up to 223 bytes and sends them as a fast packet over the
    // following run() calls. Returns false while another fast packet is in
    // progress or the address is not claimed.
    bool sendFast(std::uint8_t priority, std::uint32_t pgn, std::uint8_t destination,
                  const std::uint8_t* data, std::size_t length);
    bool fastPacketBusy() const { return fastLength_ != 0u; }

private:
    enum Pending : std::uint8_t {
        pendingClaim = 1u << 0u,
        pendingProductInfo = 1u << 1u,
        pendingTransmitList = 1u << 2u,
        pendingReceiveList = 1u << 3u,
        pendingHeartbeat = 1u << 4u,
        pendingNack = 1u << 5u,
    };

    void handleFrame(const slstm32::drivers::CanFrame& frame, std::uint32_t now);
    void handleAddressClaim(const CanId& id, const std::uint8_t* data, std::uint8_t length,
                            std::uint32_t now);
    void handleRequest(const CanId& id, const std::uint8_t* data, std::uint8_t length);
    void startClaim(std::uint32_t now);
    void servePending();
    bool sendClaim();
    bool sendHeartbeat();
    bool sendNack();
    bool beginProductInfo();
    bool beginPgnList(std::uint8_t function);
    bool beginFast(std::uint8_t priority, std::uint32_t pgn, std::uint8_t destination,
                   std::size_t length);
    void pumpFastPacket(std::uint32_t now);
    bool sendFrame(const CanId& id, const std::uint8_t* data, std::uint8_t length);
    bool nextFreeAddress(std::uint8_t& address) const;
    void markUsed(std::uint8_t address);
    bool isUsed(std::uint8_t address) const;

    slstm32::Runtime runtime_;
    slstm32::drivers::CanBusDriver& can_;
    const NodeConfig& config_;
    slstm32::Logger log_;
    MessageHandler messageHandler_{};
    void* messageContext_{};
    AddressHandler addressHandler_{};
    void* addressContext_{};

    State state_{State::idle};
    std::uint8_t address_{nullAddress};
    std::uint8_t reportedAddress_{nullAddress};
    std::uint32_t claimStartedAt_{};
    std::uint32_t nextHeartbeatAt_{};
    std::uint8_t heartbeatSequence_{};
    std::array<std::uint32_t, 8> usedAddresses_{};

    std::uint8_t pending_{};
    std::uint8_t listDestination_{broadcastAddress};
    std::uint8_t nackDestination_{broadcastAddress};
    std::uint32_t nackPgn_{};

    std::uint8_t fastData_[fastPacketCapacity]{};
    std::size_t fastLength_{};
    std::size_t fastOffset_{};
    std::uint8_t fastFrame_{};
    std::uint8_t fastSequence_{};
    CanId fastId_{};
    std::uint32_t fastProgressAt_{};

    Counters counters_{};
};

} // namespace n2k
