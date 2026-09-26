#pragma once

#include <cstdint>

namespace slstm32 {

struct Runtime {
    using MillisFn = std::uint32_t (*)();
    using CriticalFn = void (*)();
    MillisFn millis{};
    CriticalFn enterCritical{};
    CriticalFn exitCritical{};
};

class CriticalSection {
public:
    explicit CriticalSection(const Runtime& runtime) : runtime_(runtime) {
        if (runtime_.enterCritical) runtime_.enterCritical();
    }
    ~CriticalSection() {
        if (runtime_.exitCritical) runtime_.exitCritical();
    }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;

private:
    const Runtime& runtime_;
};

} // namespace slstm32
