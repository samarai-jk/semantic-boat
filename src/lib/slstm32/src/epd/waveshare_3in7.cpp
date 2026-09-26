#include "slstm32/epd/waveshare_3in7.hpp"

namespace slstm32::epd {
namespace {

constexpr std::uint8_t lutDu[105] = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x2a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x0a,0x55,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x05,0x00,0x05,0x03,0x05,0x05,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x22,0x22,0x22,0x22,0x22};
constexpr std::uint8_t lutA2[105] = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x05,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x22,0x22,0x22,0x22,0x22};
constexpr std::uint8_t lutGc[105] = {
  0x2a,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x2a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x2a,0x15,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x03,0x0a,0x00,0x02,0x06,0x0a,0x05,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x22,0x22,0x22,0x22,0x22};

constexpr Region fullRegion{0u, 0u, Waveshare3In7::panelWidth, Waveshare3In7::panelHeight};
constexpr std::size_t frameStride = (Waveshare3In7::panelWidth + 7u) / 8u;
constexpr std::size_t transferBudget = 256u;
constexpr std::uint32_t refreshTimeoutMs = 15000u;
constexpr std::uint32_t busyAssertGraceMs = 10u;
constexpr std::uint32_t settleTimeMs = 200u;

bool normalizeRegion(Region input, Region& output) {
    if (input.empty() || input.x >= Waveshare3In7::panelWidth ||
        input.y >= Waveshare3In7::panelHeight) return false;
    const auto right = static_cast<std::uint32_t>(input.x) + input.width;
    const auto bottom = static_cast<std::uint32_t>(input.y) + input.height;
    if (right > Waveshare3In7::panelWidth || bottom > Waveshare3In7::panelHeight) return false;
    const auto alignedX = static_cast<std::uint16_t>(input.x & ~std::uint16_t{7u});
    auto alignedRight = (right + 7u) & ~std::uint32_t{7u};
    if (alignedRight > Waveshare3In7::panelWidth) alignedRight = Waveshare3In7::panelWidth;
    output = {alignedX, input.y, static_cast<std::uint16_t>(alignedRight - alignedX), input.height};
    return !output.empty();
}

} // namespace

bool Waveshare3In7::waitUntilReady(std::uint32_t timeoutMs) {
    const auto started = transport_.millis();
    bool sawBusy{};
    for (;;) {
        const auto elapsed = static_cast<std::uint32_t>(transport_.millis() - started);
        const bool busy = transport_.busy();
        sawBusy = sawBusy || busy;
        if (!busy && (sawBusy || elapsed >= busyAssertGraceMs)) break;
        if (elapsed > timeoutMs) return false;
        transport_.delayMs(5u);
    }
    transport_.delayMs(settleTimeMs);
    return true;
}

bool Waveshare3In7::setWindow(Region region) {
    const auto xEnd = static_cast<std::uint16_t>(region.x + region.width - 1u);
    const auto yEnd = static_cast<std::uint16_t>(region.y + region.height - 1u);
    return command(0x44) && data(static_cast<std::uint8_t>(region.x)) &&
           data(static_cast<std::uint8_t>(region.x >> 8u)) &&
           data(static_cast<std::uint8_t>(xEnd)) && data(static_cast<std::uint8_t>(xEnd >> 8u)) &&
           command(0x45) && data(static_cast<std::uint8_t>(region.y)) &&
           data(static_cast<std::uint8_t>(region.y >> 8u)) &&
           data(static_cast<std::uint8_t>(yEnd)) && data(static_cast<std::uint8_t>(yEnd >> 8u));
}

bool Waveshare3In7::setAddress(Region region) {
    return command(0x4e) && data(static_cast<std::uint8_t>(region.x)) &&
           data(static_cast<std::uint8_t>(region.x >> 8u)) && command(0x4f) &&
           data(static_cast<std::uint8_t>(region.y)) &&
           data(static_cast<std::uint8_t>(region.y >> 8u));
}

bool Waveshare3In7::init() {
    ready_ = false;
    updateState_ = UpdateState::idle;
    pendingFrame_ = nullptr;
    transport_.power(true);
    transport_.delayMs(10u);
    transport_.reset();
    if (!command(0x12)) {
        failUpdate();
        return false;
    }
    transport_.delayMs(300u);
    if (!command(0x46) || !data(0xf7) || !waitUntilReady(5000u) ||
        !command(0x47) || !data(0xf7) || !waitUntilReady(5000u)) {
        failUpdate();
        return false;
    }

    constexpr std::uint8_t booster[] = {0x41,0xa8,0x32};
    constexpr std::uint8_t pll[] = {0xae,0xc7,0xc3,0xc0,0xc0};
    constexpr std::uint8_t analog[] = {0x00,0xff,0xff,0xff,0xff,0x4f,0xff,0xff,0xff,0xff};
    if (!command(0x01) || !data(0xdf) || !data(0x01) || !data(0x00) ||
        !command(0x03) || !data(0x00) || !command(0x04) || !transport_.writeData(booster, sizeof booster) ||
        !command(0x11) || !data(0x03) || !command(0x3c) || !data(0x00) ||
        !command(0x0c) || !transport_.writeData(pll, sizeof pll) || !command(0x18) || !data(0x80) ||
        !command(0x2c) || !data(0x44) || !command(0x37) || !transport_.writeData(analog, sizeof analog) ||
        !setWindow(fullRegion) || !command(0x22) || !data(0xcf)) {
        updateState_ = UpdateState::failed;
        return false;
    }
    ready_ = true;
    return true;
}

bool Waveshare3In7::loadLut(RefreshMode mode) {
    const auto* lut = mode == RefreshMode::partial ? lutA2 : mode == RefreshMode::fast ? lutDu : lutGc;
    return command(0x32) && transport_.writeData(lut, sizeof lutGc);
}

bool Waveshare3In7::display(const std::uint8_t* frame, std::size_t size, RefreshMode mode) {
    if (!ready_ || updateState_ != UpdateState::idle || !frame || size != bufferSize) return false;
    if (!setWindow(fullRegion) || !setAddress(fullRegion) || !command(0x24) ||
        !transport_.writeData(frame, size) || !loadLut(mode) || !command(0x20)) return false;
    ready_ = waitUntilReady(15000u);
    return ready_;
}

bool Waveshare3In7::beginDisplay(const std::uint8_t* frame, std::size_t size,
                                 RefreshMode mode, Region region) {
    if (!ready_ || updateState_ != UpdateState::idle || !frame || size != bufferSize) return false;
    if (mode != RefreshMode::partial) region = fullRegion;
    Region normalized{};
    if (!normalizeRegion(region, normalized)) return false;
    // A2 is a partial waveform on this panel, but it is not safe to treat it as
    // a partial controller-RAM transfer. A windowed upload makes untouched
    // pixels depend on stale controller RAM; persistent black areas can then
    // alternate on successive refreshes. Always refresh RAM from the complete
    // application framebuffer before activating any waveform.
    normalized = fullRegion;
    if (!setWindow(normalized) || !setAddress(normalized) || !command(0x24)) {
        failUpdate();
        return false;
    }
    pendingFrame_ = frame;
    pendingRegion_ = normalized;
    pendingMode_ = mode;
    transferRow_ = 0u;
    transferByte_ = 0u;
    updateState_ = UpdateState::transferring;
    return true;
}

bool Waveshare3In7::transferNextChunk() {
    const auto rowBytes = static_cast<std::uint16_t>(pendingRegion_.width / 8u);
    std::size_t budget = transferBudget;
    while (budget != 0u && transferRow_ < pendingRegion_.height) {
        const auto remaining = static_cast<std::uint16_t>(rowBytes - transferByte_);
        const auto amount = remaining < budget ? remaining : static_cast<std::uint16_t>(budget);
        const auto row = static_cast<std::size_t>(pendingRegion_.y + transferRow_);
        const auto column = static_cast<std::size_t>(pendingRegion_.x / 8u + transferByte_);
        const auto* source = pendingFrame_ + row * frameStride + column;
        if (!transport_.writeData(source, amount)) {
            failUpdate();
            return false;
        }
        budget -= amount;
        transferByte_ = static_cast<std::uint16_t>(transferByte_ + amount);
        if (transferByte_ == rowBytes) {
            transferByte_ = 0u;
            ++transferRow_;
        }
    }
    if (transferRow_ == pendingRegion_.height) updateState_ = UpdateState::prepared;
    return true;
}

bool Waveshare3In7::commitDisplay() {
    if (updateState_ != UpdateState::prepared) return false;
    if (!loadLut(pendingMode_) || !command(0x20)) {
        failUpdate();
        return false;
    }
    refreshStartedAt_ = transport_.millis();
    sawBusy_ = false;
    updateState_ = UpdateState::refreshing;
    return true;
}

bool Waveshare3In7::cancelDisplay() {
    if (!canCancelDisplay()) return false;
    pendingFrame_ = nullptr;
    transferRow_ = 0u;
    transferByte_ = 0u;
    updateState_ = UpdateState::idle;
    return true;
}

void Waveshare3In7::failUpdate() {
    pendingFrame_ = nullptr;
    ready_ = false;
    updateState_ = UpdateState::failed;
}

void Waveshare3In7::run() {
    if (updateState_ == UpdateState::transferring) {
        (void)transferNextChunk();
        return;
    }
    const auto now = transport_.millis();
    if (updateState_ == UpdateState::refreshing) {
        if (static_cast<std::uint32_t>(now - refreshStartedAt_) > refreshTimeoutMs) {
            failUpdate();
            return;
        }
        if (transport_.busy()) {
            sawBusy_ = true;
            return;
        }
        if (!sawBusy_ && static_cast<std::uint32_t>(now - refreshStartedAt_) < busyAssertGraceMs) return;
        settleUntil_ = now + settleTimeMs;
        updateState_ = UpdateState::settling;
        return;
    }
    if (updateState_ == UpdateState::settling &&
        static_cast<std::int32_t>(now - settleUntil_) >= 0) {
        pendingFrame_ = nullptr;
        updateState_ = UpdateState::idle;
    }
}

void Waveshare3In7::sleep() {
    if (canCancelDisplay()) (void)cancelDisplay();
    if (updateState_ != UpdateState::idle && updateState_ != UpdateState::failed) return;
    if (ready_) {
        (void)command(0x10);
        (void)data(0x01);
        transport_.delayMs(100u);
    }
    transport_.power(false);
    ready_ = false;
}

} // namespace slstm32::epd
