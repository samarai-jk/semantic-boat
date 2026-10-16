#pragma once

#include <cstddef>
#include <cstdint>

namespace slstm32::drivers {

struct DigitalInputHardware {
    void* context{};
    bool (*configure)(void*){};
    bool (*read)(void*){};
    bool activeHigh{true};
};

struct DigitalOutputHardware {
    void* context{};
    bool (*configure)(void*){};
    void (*write)(void*, bool){};
    bool activeHigh{true};
};

struct PwmOutputHardware {
    void* context{};
    bool (*configure)(void*){};
    bool (*start)(void*){};
    void (*stop)(void*){};
    void (*write)(void*, float){};
};

struct ToneOutputHardware {
    void* context{};
    bool (*configure)(void*){};
    bool (*start)(void*, std::uint32_t, float){};
    void (*stop)(void*){};
};

// Byte-serial output such as a UART transmitter. write() must not block: it
// accepts as many leading bytes as the hardware can take right now (possibly
// none) and returns that count.
struct SerialOutputHardware {
    void* context{};
    bool (*configure)(void*){};
    std::size_t (*write)(void*, const std::uint8_t*, std::size_t){};
};

// Byte-serial input such as a UART receiver. read() must not block: it copies
// up to `size` bytes that have already arrived and returns how many.
struct SerialInputHardware {
    void* context{};
    bool (*configure)(void*){};
    std::size_t (*read)(void*, std::uint8_t*, std::size_t){};
};

struct AnalogInputHardware {
    void* context{};
    bool (*configure)(void*){};
    bool (*sample)(void*, std::int32_t&){};
};

} // namespace slstm32::drivers
