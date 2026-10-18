#pragma once

#include <cstddef>
#include <cstdint>
#include "hardware.hpp"
#include "n2k/can_id.hpp"

namespace boot {

// The bootloader on B1S (src/lib/b1s/FIRMWARE.md): a minimal node with the
// device's own unique number, address claim with contention, product
// information and heartbeat, and the firmware update services. Single
// threaded and polled; one fast packet received at a time.
class Node {
public:
    // reason: why the bootloader stays (reported as model version).
    explicit Node(const char* reason) : reason_(reason) {}

    void start();
    void run();

private:
    enum class Status : std::uint8_t {
        done = 0u, unknown = 1u, invalidValue = 3u, busy = 5u, wrongBoard = 7u, tooLarge = 8u, flashError = 9u,
        unexpectedOffset = 10u, crcMismatch = 11u,
    };

    void handle(const hw::Frame& frame);
    void handleClaim(const n2k::CanId& id, const std::uint8_t* data);
    void handleRequest(const n2k::CanId& id, const std::uint8_t* data);
    void receiveFast(const n2k::CanId& id, const std::uint8_t* data, std::uint8_t length);
    void handleService(std::uint8_t requester, const std::uint8_t* data, std::size_t length);
    void handleCommand(std::uint8_t requester, const std::uint8_t* data);
    void claim(std::uint32_t now);
    bool sendSingle(std::uint8_t priority, std::uint32_t pgn, std::uint8_t destination, const std::uint8_t* data,
                    std::uint8_t length);
    bool sendFast(std::uint8_t priority, std::uint32_t pgn, std::uint8_t destination, const std::uint8_t* data,
                  std::size_t length);
    void sendClaim();
    void sendProductInformation();
    void sendHeartbeat();

    Status begin(const std::uint8_t* header);
    Status write(std::uint32_t offset, const std::uint8_t* data, std::size_t length);
    Status end();

    const char* reason_;
    std::uint64_t name_{};
    std::uint8_t address_{};
    bool claimed_{};
    std::uint32_t claimAt_{};
    std::uint32_t heartbeatAt_{};
    std::uint32_t restartAt_{};
    std::uint32_t identifyUntil_{};
    std::uint8_t heartbeatSequence_{};
    std::uint8_t fastSequence_{};

    // Fast packet being received (PGN 126720 to this node).
    std::uint8_t rxData_[223]{};
    std::size_t rxLength_{};
    std::size_t rxOffset_{};
    std::uint8_t rxSource_{};
    std::uint8_t rxSequence_{};
    std::uint8_t rxNextFrame_{};
    std::uint32_t rxAt_{};

    // Update in progress.
    bool updating_{};
    std::uint8_t header_[64]{};
    std::uint32_t size_{};
    std::uint32_t next_{};
    std::uint32_t erasedPages_{};
};

} // namespace boot
