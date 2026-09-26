#include "navigation_service.hpp"
#include "events.hpp"
#include "slstm32/drivers/button.hpp"
#include "slstm32/epd/font5x7.hpp"
#include <cstdio>

namespace remote_a {
using slstm32::epd::Font5x7;
using slstm32::epd::RefreshMode;
using slstm32::epd::Region;
using slstm32::epd::UpdateState;

namespace {
constexpr std::uint32_t inputSettleMs = 125u;
}

bool NavigationService::init() {
    if (!events_.subscribe(buttonPressed, &NavigationService::eventThunk, this)) return false;
    render();
    return panel_.display(canvas_.data(), canvas_.size(), RefreshMode::full);
}

void NavigationService::eventThunk(slstm32::EventId, const void* payload, std::uint8_t size, void* context) {
    if (!payload || size != sizeof(slstm32::drivers::ButtonEvent) || !context) return;
    const auto& event = *static_cast<const slstm32::drivers::ButtonEvent*>(payload);
    if (event.pressed) static_cast<NavigationService*>(context)->onButton(event.id);
}

void NavigationService::onButton(std::uint8_t index) {
    if (index == 0u || index == 1u) {
        adjust(index == 0u ? 1 : -1);
        invalidate(RenderRegion::primary);
        invalidate(selected_);
    } else if (index <= 5u) {
        const auto previous = selected_;
        selected_ = static_cast<Field>(index - 2u);
        invalidate(RenderRegion::primary);
        invalidate(previous);
        invalidate(selected_);
    } else return;
    preemptCancelableUpdate();
    refreshAt_ = runtime_.millis ? runtime_.millis() + inputSettleMs : 0u;
}

void NavigationService::adjust(int direction) {
    auto wrapDegrees = [direction](std::uint16_t value) {
        return direction > 0 ? static_cast<std::uint16_t>((value + 1u) % 360u)
                             : static_cast<std::uint16_t>((value + 359u) % 360u);
    };
    auto wrapTenths = [direction](std::uint16_t value) {
        return direction > 0 ? static_cast<std::uint16_t>(value < 999u ? value + 1u : 0u)
                             : static_cast<std::uint16_t>(value > 0u ? value - 1u : 999u);
    };
    switch (selected_) {
    case Field::heading: data_.heading = wrapDegrees(data_.heading); break;
    case Field::course: data_.course = wrapDegrees(data_.course); break;
    case Field::speed: data_.speedTenths = wrapTenths(data_.speedTenths); break;
    case Field::distance: data_.distanceTenths = wrapTenths(data_.distanceTenths); break;
    }
}

void NavigationService::invalidate(RenderRegion region) {
    (void)renderQueue_.request(static_cast<std::uint8_t>(region));
}

void NavigationService::invalidate(Field field) {
    invalidate(static_cast<RenderRegion>(static_cast<std::uint8_t>(field) + 1u));
}

void NavigationService::preemptCancelableUpdate() {
    if (!renderQueue_.active() || !panel_.canCancelDisplay() || !panel_.cancelDisplay()) return;
    renderQueue_.retryActive();
    fullRefreshPending_ = fullRefreshPending_ || activeFullRefresh_;
    activeFullRefresh_ = false;
}

Region NavigationService::logicalRegion(RenderRegion region) const {
    switch (region) {
    case RenderRegion::primary: return {10u, 52u, 290u, 178u};
    case RenderRegion::heading: return {312u, 52u, 158u, 40u};
    case RenderRegion::speed: return {312u, 97u, 158u, 40u};
    case RenderRegion::course: return {312u, 142u, 158u, 40u};
    case RenderRegion::distance: return {312u, 187u, 158u, 40u};
    }
    return {};
}

Region NavigationService::pendingRegion(std::uint32_t mask) const {
    Region result{};
    for (std::uint8_t index = 0u; index < renderRegionCount; ++index) {
        if ((mask & (std::uint32_t{1u} << index)) == 0u) continue;
        result = slstm32::epd::unite(result, logicalRegion(static_cast<RenderRegion>(index)));
    }
    return result;
}

const char* NavigationService::label(Field field) const {
    switch (field) {
    case Field::speed: return "SPEED";
    case Field::course: return "COURSE";
    case Field::distance: return "DISTANCE";
    default: return "HEADING";
    }
}

const char* NavigationService::unit(Field field) const {
    switch (field) {
    case Field::speed: return "KT";
    case Field::distance: return "NM";
    case Field::course: return "DEG";
    default: return "DEG TRUE";
    }
}

void NavigationService::format(Field field, char* output, std::uint16_t size) const {
    switch (field) {
    case Field::speed:
        std::snprintf(output, size, "%u.%u", data_.speedTenths / 10u, data_.speedTenths % 10u); break;
    case Field::distance:
        std::snprintf(output, size, "%u.%u", data_.distanceTenths / 10u, data_.distanceTenths % 10u); break;
    case Field::course:
        std::snprintf(output, size, "%03u", data_.course); break;
    default:
        std::snprintf(output, size, "%03u", data_.heading); break;
    }
}

void NavigationService::drawCenteredBox(std::uint16_t x, std::uint16_t width, std::uint16_t y,
                                        const char* text, bool black, std::uint8_t scale) {
    const auto textWidth = Font5x7::textWidth(text, scale);
    const auto textX = textWidth < width ? static_cast<std::uint16_t>(x + (width - textWidth) / 2u) : x;
    Font5x7::drawText(canvas_, textX, y, text, black, scale);
}

void NavigationService::drawSummary(std::uint16_t y, Field field) {
    char value[16]{};
    format(field, value, sizeof value);
    const bool active = selected_ == field;
    if (active) canvas_.fillRect(312u, y, 158u, 40u, true);
    else canvas_.drawRect(312u, y, 158u, 40u, true, 2u);
    Font5x7::drawText(canvas_, 320u, static_cast<std::uint16_t>(y + 6u), label(field), !active, 1u);
    drawCenteredBox(312u, 158u, static_cast<std::uint16_t>(y + 20u), value, !active, 2u);
}

void NavigationService::render() {
    char value[16]{};
    char trip[16]{};
    format(selected_, value, sizeof value);
    std::snprintf(trip, sizeof trip, "%u.%u", data_.tripTenths / 10u, data_.tripTenths % 10u);
    canvas_.clear(false);
    canvas_.fillRect(0u, 0u, canvas_.width(), 42u, true);
    Font5x7::drawCentered(canvas_, 10u, "SEMANTIC BOAT", false, 3u);
    canvas_.drawRect(10u, 52u, 290u, 178u, true, 2u);
    drawCenteredBox(10u, 290u, 65u, label(selected_), true, 3u);
    drawCenteredBox(10u, 290u, 101u, value, true, 9u);
    drawCenteredBox(10u, 290u, 184u, unit(selected_), true, 3u);
    drawSummary(52u, Field::heading);
    drawSummary(97u, Field::speed);
    drawSummary(142u, Field::course);
    drawSummary(187u, Field::distance);
    canvas_.fillRect(0u, 240u, canvas_.width(), 40u, true);
    Font5x7::drawText(canvas_, 10u, 247u, "B0 UP  B1 DOWN", false, 1u);
    Font5x7::drawText(canvas_, 10u, 264u, "B2 HDG  B3 SPD  B4 CRS  B5 DST", false, 1u);
    Font5x7::drawText(canvas_, 374u, 247u, "TRIP", false, 1u);
    Font5x7::drawText(canvas_, 410u, 247u, trip, false, 1u);
}

void NavigationService::run() {
    const auto now = runtime_.millis ? runtime_.millis() : 0u;

    if (panel_.updateState() == UpdateState::failed) {
        if (renderQueue_.active()) renderQueue_.retryActive();
        fullRefreshPending_ = fullRefreshPending_ || activeFullRefresh_;
        activeFullRefresh_ = false;
        if (runtime_.millis && static_cast<std::int32_t>(now - recoverAt_) < 0) return;
        recoverAt_ = now + 1000u;
        (void)panel_.init();
        return;
    }

    if (renderQueue_.pending() && panel_.canCancelDisplay()) {
        if (panel_.cancelDisplay()) {
            renderQueue_.retryActive();
            fullRefreshPending_ = fullRefreshPending_ || activeFullRefresh_;
            activeFullRefresh_ = false;
        }
    }

    if (renderQueue_.active()) {
        if (panel_.updateState() == UpdateState::prepared) {
            (void)panel_.commitDisplay();
        } else if (panel_.updateState() == UpdateState::idle) {
            renderQueue_.complete();
            if (activeFullRefresh_) partialRefreshCount_ = 0u;
            else if (partialRefreshCount_ != UINT32_MAX) ++partialRefreshCount_;
            activeFullRefresh_ = false;
        }
        return;
    }

    if (!renderQueue_.pending()) return;
    if (runtime_.millis && static_cast<std::int32_t>(now - refreshAt_) < 0) return;

    const auto mask = renderQueue_.begin();
    render();
    const auto fullRefreshLimit = refreshPolicy_.fullRefreshAfterPartialUpdates;
    const bool fullRefresh = fullRefreshPending_ ||
        (fullRefreshLimit != 0u && partialRefreshCount_ >= fullRefreshLimit);
    const auto logical = fullRefresh
        ? Region{0u, 0u, canvas_.width(), canvas_.height()}
        : pendingRegion(mask);
    const auto raw = canvas_.toRawRegion(logical);
    const auto mode = fullRefresh ? RefreshMode::full : RefreshMode::partial;
    if (!panel_.beginDisplay(canvas_.data(), canvas_.size(), mode, raw)) {
        renderQueue_.retryActive();
        return;
    }
    activeFullRefresh_ = fullRefresh;
    if (fullRefresh) fullRefreshPending_ = false;
}

} // namespace remote_a
