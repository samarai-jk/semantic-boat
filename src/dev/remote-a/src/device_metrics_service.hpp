#pragma once

#include <array>
#include <string_view>
#include "display_link_service.hpp"
#include "semantic_display/data.hpp"
#include "semantic_display/display.hpp"
#include "semantic_display/history.hpp"
#include "semantic_display/package.hpp"
#include "slstm32/event_bus.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"
#include "uart_transport.hpp"

namespace remote_a {

class DeviceMetricsService final : public slstm32::Service {
public:
    DeviceMetricsService(slstm32::Runtime runtime,
                         semantic_display::PackageView& package,
                         semantic_display::DataStore& data,
                         semantic_display::HistoryStore& history,
                         semantic_display::DisplayService& display,
                         slstm32::EventBus& events, UartTransport& transport,
                         DisplayLinkService& link)
        : runtime_(runtime), package_(package), data_(data), history_(history),
          display_(display), events_(events), transport_(transport), link_(link) {}

    static void beginStackMonitoring();
    bool init() override;
    void run() override;
    void setDeveloperMode(bool enabled);
    std::string_view headerStatus() const {
        return developerMode_ ? std::string_view{headerStatus_.data()} : std::string_view{};
    }

private:
    bool publishNumber(const semantic_display::SourceView& source, float value,
                       std::uint32_t now);
    bool publishText(const semantic_display::SourceView& source, const char* value,
                     std::uint32_t now, bool requestRender = false);
    void update(std::uint32_t now);

    slstm32::Runtime runtime_;
    semantic_display::PackageView& package_;
    semantic_display::DataStore& data_;
    semantic_display::HistoryStore& history_;
    semantic_display::DisplayService& display_;
    slstm32::EventBus& events_;
    UartTransport& transport_;
    DisplayLinkService& link_;
    std::uint32_t updateAt_{};
    std::array<char, 24> headerStatus_{};
    bool developerMode_{};
};

} // namespace remote_a
