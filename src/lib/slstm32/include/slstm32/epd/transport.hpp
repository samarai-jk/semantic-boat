#pragma once

#include <cstddef>
#include <cstdint>
#include "slstm32/driver.hpp"
#include "slstm32/epd/geometry.hpp"

namespace slstm32::epd {

enum class RefreshMode : std::uint8_t { full, fast, partial };

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool writeCommand(std::uint8_t command) = 0;
    virtual bool writeData(const std::uint8_t* data, std::size_t size) = 0;
    virtual void reset() = 0;
    virtual void power(bool enabled) = 0;
    virtual bool busy() const = 0;
    virtual void delayMs(std::uint32_t duration) = 0;
    virtual std::uint32_t millis() const = 0;
};

class Panel : public Driver {
public:
    virtual std::uint16_t width() const = 0;
    virtual std::uint16_t height() const = 0;
    virtual std::size_t frameSize() const = 0;
    virtual bool display(const std::uint8_t* frame, std::size_t size, RefreshMode mode) = 0;
    virtual void sleep() = 0;
};

enum class UpdateState : std::uint8_t {
    idle,
    transferring,
    prepared,
    refreshing,
    settling,
    failed,
};

class AsyncPanel : public Panel {
public:
    virtual bool beginDisplay(const std::uint8_t* frame, std::size_t size,
                              RefreshMode mode, Region region) = 0;
    virtual bool commitDisplay() = 0;
    virtual bool cancelDisplay() = 0;
    virtual UpdateState updateState() const = 0;

    bool updateInProgress() const {
        const auto state = updateState();
        return state != UpdateState::idle && state != UpdateState::failed;
    }
    bool canCancelDisplay() const {
        const auto state = updateState();
        return state == UpdateState::transferring || state == UpdateState::prepared;
    }
};

} // namespace slstm32::epd
