#pragma once

#include <cstddef>
#include <cstdint>
#include "semantic_link/protocol.hpp"

namespace boot {

// The bootloader on the RS485 bus: an addressed semantic-link endpoint that
// answers only the gateway's requests to its address (a retried request gets
// the cached reply), with the firmware update requests (semantic-link
// PROTOCOL.md, "Firmware update"). Everything else gets an empty Idle reply.
class Link {
public:
    // reason: why the bootloader stays (reported in the firmware status).
    Link(std::uint8_t address, const char* reason) : address_(address), reason_(reason) {}

    void start();
    void run();
    bool updating() const { return updating_; }

private:
    using Result = semantic_link::FirmwareResult;

    void handle(const semantic_link::MessageView& message);
    void reportStatus(Result result);
    Result begin(const std::uint8_t* header, std::size_t size);
    Result write(const std::uint8_t* payload, std::size_t size);
    Result end();

    std::uint8_t address_;
    const char* reason_;
    semantic_link::Decoder decoder_{};
    semantic_link::PolledEndpoint endpoint_{};
    std::uint32_t restartAt_{};
    // Diagnosis, reported in the status text: requests, retries answered from
    // the cache, replies too late to send, bytes with a line error.
    std::uint32_t requests_{}, duplicates_{}, late_{}, lineErrors_{};
    bool restartPending_{};

    // Update in progress.
    bool updating_{};
    std::uint8_t header_[64]{};
    std::uint32_t size_{};
    std::uint32_t next_{};
    std::uint32_t erasedPages_{};
    std::uint32_t crc_{}; // of the bytes written so far, CRC field as 0xFF
};

} // namespace boot
