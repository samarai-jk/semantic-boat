#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/drivers/hardware.hpp"
#include "slstm32/log.hpp"
#include "slstm32/runtime.hpp"

namespace slstm32::drivers {

// Line-oriented log output over a byte-serial port such as a UART.
//
// write() prefixes each message with a millisecond timestamp and a level
// letter and copies the complete line into a caller-provided ring buffer.
// run() drains the ring through the non-blocking hardware write callback, so
// logging never stalls the application loop. A line that does not fit is
// dropped whole and counted. write() may be called from an interrupt when the
// runtime critical-section hooks are interrupt-safe. Lines written before
// init() are buffered and sent once the driver runs.
class SerialLogDriver final : public Driver {
public:
    SerialLogDriver(Runtime runtime, SerialOutputHardware hardware,
                    std::uint8_t* buffer, std::size_t capacity,
                    LogLevel minimumLevel = LogLevel::debug)
        : runtime_(runtime), hardware_(hardware), buffer_(buffer), capacity_(capacity),
          minimumLevel_(minimumLevel) {}

    bool init() override;
    void run() override;
    void write(LogLevel level, const char* message);
    // Sends pending output synchronously, for use before a reset or another
    // terminal state. Requires a running millisecond clock. Returns false if
    // output remained when the timeout expired.
    bool flush(std::uint32_t timeoutMs);
    Logger logger() { return Logger{&sink, this}; }
    void setMinimumLevel(LogLevel level) { minimumLevel_ = level; }
    std::size_t pending() const;
    std::uint32_t droppedLines() const { return dropped_; }

private:
    static void sink(LogLevel level, const char* message, void* context);
    void push(const char* data, std::size_t size);

    Runtime runtime_;
    SerialOutputHardware hardware_;
    std::uint8_t* buffer_{};
    std::size_t capacity_{};
    LogLevel minimumLevel_{};
    volatile std::size_t head_{};
    volatile std::size_t tail_{};
    volatile std::uint32_t dropped_{};
};

} // namespace slstm32::drivers
