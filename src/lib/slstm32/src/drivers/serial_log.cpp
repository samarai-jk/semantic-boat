#include "slstm32/drivers/serial_log.hpp"
#include <cstring>

namespace slstm32::drivers {

namespace {

constexpr std::size_t secondsWidth = 6u;

char levelLetter(LogLevel level) {
    switch (level) {
    case LogLevel::debug: return 'D';
    case LogLevel::info: return 'I';
    case LogLevel::warning: return 'W';
    case LogLevel::error: return 'E';
    }
    return '?';
}

// Formats "[ssssss.mmm] L " without pulling printf into the driver.
std::size_t formatPrefix(char* output, std::uint32_t nowMs, LogLevel level) {
    std::size_t length = 0u;
    output[length++] = '[';
    auto seconds = nowMs / 1000u;
    const auto milliseconds = nowMs % 1000u;
    char digits[10];
    std::size_t count = 0u;
    do {
        digits[count++] = static_cast<char>('0' + seconds % 10u);
        seconds /= 10u;
    } while (seconds != 0u && count < sizeof digits);
    for (auto pad = count; pad < secondsWidth; ++pad) output[length++] = ' ';
    while (count != 0u) output[length++] = digits[--count];
    output[length++] = '.';
    output[length++] = static_cast<char>('0' + milliseconds / 100u);
    output[length++] = static_cast<char>('0' + milliseconds / 10u % 10u);
    output[length++] = static_cast<char>('0' + milliseconds % 10u);
    output[length++] = ']';
    output[length++] = ' ';
    output[length++] = levelLetter(level);
    output[length++] = ' ';
    return length;
}

} // namespace

bool SerialLogDriver::init() {
    if (!hardware_.write || !buffer_ || capacity_ < 2u) return false;
    return !hardware_.configure || hardware_.configure(hardware_.context);
}

std::size_t SerialLogDriver::pending() const {
    const std::size_t head = head_;
    const std::size_t tail = tail_;
    return head >= tail ? head - tail : capacity_ - tail + head;
}

void SerialLogDriver::push(const char* data, std::size_t size) {
    std::size_t head = head_;
    for (std::size_t index = 0; index < size; ++index) {
        buffer_[head] = static_cast<std::uint8_t>(data[index]);
        head = head + 1u == capacity_ ? 0u : head + 1u;
    }
    head_ = head;
}

void SerialLogDriver::write(LogLevel level, const char* message) {
    if (!buffer_ || capacity_ < 2u || !message || level < minimumLevel_) return;
    char prefix[24];
    const auto prefixLength = formatPrefix(prefix, runtime_.millis ? runtime_.millis() : 0u, level);
    const auto messageLength = std::strlen(message);
    const auto lineLength = prefixLength + messageLength + 2u;

    CriticalSection lock(runtime_);
    // One slot stays empty so that head == tail always means "empty".
    if (capacity_ - 1u - pending() < lineLength) {
        dropped_ = dropped_ + 1u;
        return;
    }
    push(prefix, prefixLength);
    push(message, messageLength);
    push("\r\n", 2u);
}

void SerialLogDriver::run() {
    if (!hardware_.write || !buffer_) return;
    for (;;) {
        const std::size_t head = head_;
        const std::size_t tail = tail_;
        if (head == tail) return;
        const std::size_t chunk = head > tail ? head - tail : capacity_ - tail;
        auto sent = hardware_.write(hardware_.context, buffer_ + tail, chunk);
        if (sent == 0u) return;
        if (sent > chunk) sent = chunk;
        tail_ = (tail + sent) % capacity_;
    }
}

bool SerialLogDriver::flush(std::uint32_t timeoutMs) {
    if (!runtime_.millis) return false;
    const auto startedAt = runtime_.millis();
    for (;;) {
        run();
        if (pending() == 0u) return true;
        if (static_cast<std::uint32_t>(runtime_.millis() - startedAt) >= timeoutMs) return false;
    }
}

void SerialLogDriver::sink(LogLevel level, const char* message, void* context) {
    static_cast<SerialLogDriver*>(context)->write(level, message);
}

} // namespace slstm32::drivers
