#include "slstm32/drivers/can_bus.hpp"

namespace slstm32::drivers {

namespace {
std::size_t next(std::size_t index, std::size_t capacity) {
    return index + 1u == capacity ? 0u : index + 1u;
}
} // namespace

bool CanBusDriver::init() {
    if (!hardware_.transmit || !hardware_.start || !receiveBuffer_ || receiveCapacity_ < 2u ||
        !transmitBuffer_ || transmitCapacity_ < 2u) {
        return false;
    }
    if (hardware_.configure && !hardware_.configure(hardware_.context)) return false;
    started_ = hardware_.start(hardware_.context);
    return started_;
}

void CanBusDriver::run() {
    if (!started_) return;
    CriticalSection lock(runtime_);
    pumpTransmitQueue();
}

bool CanBusDriver::send(const CanFrame& frame) {
    CriticalSection lock(runtime_);
    const auto head = transmitHead_;
    const auto following = next(head, transmitCapacity_);
    if (following == transmitTail_) {
        ++counters_.transmitDropped;
        return false;
    }
    transmitBuffer_[head] = frame;
    transmitHead_ = following;
    if (started_) pumpTransmitQueue();
    return true;
}

std::size_t CanBusDriver::transmitSpace() const {
    CriticalSection lock(runtime_);
    const std::size_t head = transmitHead_;
    const std::size_t tail = transmitTail_;
    const auto used = head >= tail ? head - tail : transmitCapacity_ - tail + head;
    return transmitCapacity_ - 1u - used;
}

bool CanBusDriver::receive(CanFrame& frame) {
    CriticalSection lock(runtime_);
    const auto tail = receiveTail_;
    if (tail == receiveHead_) return false;
    frame = receiveBuffer_[tail];
    receiveTail_ = next(tail, receiveCapacity_);
    return true;
}

CanStatus CanBusDriver::status() const {
    if (!started_ || !hardware_.status) return {};
    return hardware_.status(hardware_.context);
}

CanBusDriver::Counters CanBusDriver::counters() const {
    CriticalSection lock(runtime_);
    return counters_;
}

void CanBusDriver::onFrameReceived(const CanFrame& frame) {
    CriticalSection lock(runtime_);
    const auto head = receiveHead_;
    const auto following = next(head, receiveCapacity_);
    if (following == receiveTail_) {
        ++counters_.receiveDropped;
        return;
    }
    receiveBuffer_[head] = frame;
    receiveHead_ = following;
    ++counters_.received;
}

void CanBusDriver::onTransmitComplete() {
    CriticalSection lock(runtime_);
    ++counters_.transmitted;
    pumpTransmitQueue();
}

void CanBusDriver::onReceiveOverrun() {
    CriticalSection lock(runtime_);
    ++counters_.hardwareOverruns;
}

// Caller holds the critical section.
void CanBusDriver::pumpTransmitQueue() {
    while (transmitTail_ != transmitHead_) {
        const auto tail = transmitTail_;
        if (!hardware_.transmit(hardware_.context, transmitBuffer_[tail])) return;
        transmitTail_ = next(tail, transmitCapacity_);
    }
}

} // namespace slstm32::drivers
