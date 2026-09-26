#pragma once

#include "slstm32/epd/transport.hpp"

namespace slstm32::epd {

class Waveshare3In7 final : public AsyncPanel {
public:
    static constexpr std::uint16_t panelWidth = 280u;
    static constexpr std::uint16_t panelHeight = 480u;
    static constexpr std::size_t bufferSize = panelWidth * panelHeight / 8u;

    explicit Waveshare3In7(Transport& transport) : transport_(transport) {}
    bool init() override;
    void run() override;
    std::uint16_t width() const override { return panelWidth; }
    std::uint16_t height() const override { return panelHeight; }
    std::size_t frameSize() const override { return bufferSize; }
    bool display(const std::uint8_t* frame, std::size_t size, RefreshMode mode) override;
    bool beginDisplay(const std::uint8_t* frame, std::size_t size,
                      RefreshMode mode, Region region) override;
    bool commitDisplay() override;
    bool cancelDisplay() override;
    UpdateState updateState() const override { return updateState_; }
    void sleep() override;

private:
    bool command(std::uint8_t value) { return transport_.writeCommand(value); }
    bool data(std::uint8_t value) { return transport_.writeData(&value, 1u); }
    bool waitUntilReady(std::uint32_t timeoutMs);
    bool loadLut(RefreshMode mode);
    bool setWindow(Region region);
    bool setAddress(Region region);
    bool transferNextChunk();
    void failUpdate();

    Transport& transport_;
    const std::uint8_t* pendingFrame_{};
    Region pendingRegion_{};
    RefreshMode pendingMode_{RefreshMode::partial};
    UpdateState updateState_{UpdateState::idle};
    std::uint16_t transferRow_{};
    std::uint16_t transferByte_{};
    std::uint32_t refreshStartedAt_{};
    std::uint32_t settleUntil_{};
    bool sawBusy_{};
    bool ready_{};
};

} // namespace slstm32::epd
