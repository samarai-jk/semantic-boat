#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/runtime.hpp"

namespace slstm32::drivers {

struct CanFrame {
    std::uint32_t id{};
    bool extended{};
    bool remote{};
    std::uint8_t length{};
    std::array<std::uint8_t, 8> data{};
};

enum class CanState : std::uint8_t { stopped, errorActive, errorWarning, errorPassive, busOff };

struct CanStatus {
    CanState state{CanState::stopped};
    std::uint8_t transmitErrors{};
    std::uint8_t receiveErrors{};
    // Controller-specific last error code; 0 means none.
    std::uint8_t lastErrorCode{};
};

// Board binding for a CAN controller. transmit() hands one frame to a free
// hardware mailbox and returns false when none is free. The binding forwards
// its receive, transmit-complete, and overrun interrupts to the driver.
struct CanBusHardware {
    void* context{};
    bool (*configure)(void*){};
    bool (*start)(void*){};
    void (*stop)(void*){};
    bool (*transmit)(void*, const CanFrame&){};
    CanStatus (*status)(void*){};
};

// Queued CAN access for the cooperative loop. Received frames are copied into
// a caller-provided ring from the receive interrupt and read with receive().
// send() queues a frame; queued frames move into hardware mailboxes from
// run() and from the transmit-complete interrupt, in order. Full queues drop
// frames and count them. Requires interrupt-safe runtime critical sections.
class CanBusDriver final : public Driver {
public:
    struct Counters {
        std::uint32_t received{};
        std::uint32_t transmitted{};
        std::uint32_t receiveDropped{};
        std::uint32_t transmitDropped{};
        std::uint32_t hardwareOverruns{};
    };

    CanBusDriver(Runtime runtime, CanBusHardware hardware, CanFrame* receiveBuffer,
                 std::size_t receiveCapacity, CanFrame* transmitBuffer,
                 std::size_t transmitCapacity)
        : runtime_(runtime), hardware_(hardware), receiveBuffer_(receiveBuffer),
          receiveCapacity_(receiveCapacity), transmitBuffer_(transmitBuffer),
          transmitCapacity_(transmitCapacity) {}

    bool init() override;
    void run() override;
    bool send(const CanFrame& frame);
    // Frames that send() can accept right now without dropping.
    std::size_t transmitSpace() const;
    bool receive(CanFrame& frame);
    CanStatus status() const;
    Counters counters() const;

    // Interrupt side, called by the board binding.
    void onFrameReceived(const CanFrame& frame);
    void onTransmitComplete();
    void onReceiveOverrun();

private:
    void pumpTransmitQueue();

    Runtime runtime_;
    CanBusHardware hardware_;
    CanFrame* receiveBuffer_{};
    std::size_t receiveCapacity_{};
    CanFrame* transmitBuffer_{};
    std::size_t transmitCapacity_{};
    volatile std::size_t receiveHead_{};
    volatile std::size_t receiveTail_{};
    volatile std::size_t transmitHead_{};
    volatile std::size_t transmitTail_{};
    Counters counters_{};
    bool started_{};
};

} // namespace slstm32::drivers
