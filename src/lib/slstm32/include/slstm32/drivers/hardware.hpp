#pragma once

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

struct AnalogInputHardware {
    void* context{};
    bool (*configure)(void*){};
    bool (*sample)(void*, std::int32_t&){};
};

} // namespace slstm32::drivers
