#pragma once

#include <cstdint>
#include "slstm32/epd/monochrome_canvas.hpp"
#include "slstm32/epd/render_queue.hpp"
#include "slstm32/epd/transport.hpp"
#include "slstm32/event_bus.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace remote_a {

class NavigationService final : public slstm32::Service {
public:
    struct RefreshPolicy {
        // Zero disables periodic forced full refreshes.
        std::uint32_t fullRefreshAfterPartialUpdates{};
    };

    NavigationService(slstm32::Runtime runtime, slstm32::EventBus& events,
                      slstm32::epd::AsyncPanel& panel,
                      slstm32::epd::MonochromeCanvas& canvas)
        : NavigationService(runtime, events, panel, canvas, RefreshPolicy{0u}) {}

    NavigationService(slstm32::Runtime runtime, slstm32::EventBus& events,
                      slstm32::epd::AsyncPanel& panel,
                      slstm32::epd::MonochromeCanvas& canvas,
                      RefreshPolicy refreshPolicy)
        : runtime_(runtime), events_(events), panel_(panel), canvas_(canvas),
          refreshPolicy_(refreshPolicy) {}
    bool init() override;
    void run() override;

private:
    enum class Field : std::uint8_t { heading, speed, course, distance };
    enum class RenderRegion : std::uint8_t { primary, heading, speed, course, distance };
    static constexpr std::size_t renderRegionCount = 5u;
    struct Data {
        std::uint16_t heading{123u};
        std::uint16_t course{128u};
        std::uint16_t speedTenths{64u};
        std::uint16_t distanceTenths{32u};
        std::uint16_t tripTenths{187u};
    };

    static void eventThunk(slstm32::EventId, const void*, std::uint8_t, void*);
    void onButton(std::uint8_t index);
    void adjust(int direction);
    void invalidate(RenderRegion region);
    void invalidate(Field field);
    void preemptCancelableUpdate();
    slstm32::epd::Region logicalRegion(RenderRegion region) const;
    slstm32::epd::Region pendingRegion(std::uint32_t mask) const;
    void render();
    const char* label(Field field) const;
    const char* unit(Field field) const;
    void format(Field field, char* output, std::uint16_t size) const;
    void drawCenteredBox(std::uint16_t x, std::uint16_t width, std::uint16_t y,
                         const char* text, bool black, std::uint8_t scale);
    void drawSummary(std::uint16_t y, Field field);

    slstm32::Runtime runtime_;
    slstm32::EventBus& events_;
    slstm32::epd::AsyncPanel& panel_;
    slstm32::epd::MonochromeCanvas& canvas_;
    RefreshPolicy refreshPolicy_;
    slstm32::epd::RenderQueue<renderRegionCount> renderQueue_{};
    Data data_{};
    Field selected_{Field::heading};
    std::uint32_t refreshAt_{};
    std::uint32_t recoverAt_{};
    std::uint32_t partialRefreshCount_{};
    bool fullRefreshPending_{};
    bool activeFullRefresh_{};
};

} // namespace remote_a
