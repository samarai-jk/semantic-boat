#include "semantic_display/display.hpp"
#include "slstm32/epd/font5x7.hpp"
#include <cstdio>
#include <cstring>

namespace semantic_display {
namespace {

using slstm32::epd::Font5x7;
using slstm32::epd::RefreshMode;
using slstm32::epd::Region;
using slstm32::epd::UpdateState;

constexpr std::uint16_t horizontalSafeMargin = 7u;
constexpr std::uint16_t headerHeight = 30u;
constexpr std::uint16_t footerHeight = 28u;
constexpr std::uint16_t outerGap = 4u;

void copyText(char* destination, std::size_t capacity, const char* source) {
    if (!destination || capacity == 0u) return;
    std::size_t index{};
    if (source) while (index + 1u < capacity && source[index]) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

void copyView(char* destination, std::size_t capacity, std::string_view source) {
    if (!destination || capacity == 0u) return;
    const auto length = source.size() < capacity - 1u ? source.size() : capacity - 1u;
    if (length != 0u) std::memcpy(destination, source.data(), length);
    destination[length] = '\0';
}

void fitText(char* text, std::size_t capacity, std::uint16_t width, std::uint8_t scale) {
    if (!text || capacity == 0u) return;
    std::size_t length{};
    while (length + 1u < capacity && text[length]) ++length;
    const bool truncated = Font5x7::textWidth(text, scale) > width;
    while (length != 0u && Font5x7::textWidth(text, scale) > width) {
        text[--length] = '\0';
    }
    if (truncated && length > 1u) text[length - 1u] = '.';
}

} // namespace

bool DisplayService::setPackage(PackageView package) {
    if (!package.valid()) return false;
    package_ = package;
    configured_ = true;
    return true;
}

bool DisplayService::init() {
    if (!configured_ || !runtime_.millis || !data_.configure(package_) ||
        !history_.configure(package_, runtime_.millis()) || !selectFirstPage()) return false;
    if (navigationNotification_.changed) {
        navigationNotification_.changed(navigationNotification_.context, activeSection_, activePage_);
    }
    clockRefreshAt_ = runtime_.millis() + policy_.localClockRenderIntervalMs;
    render();
    dirty_ = false;
    return panel_.display(canvas_.data(), canvas_.size(), RefreshMode::full);
}

bool DisplayService::selectFirstPage() {
    activeSection_ = noIndex;
    activePage_ = noIndex;
    RecordView record{};
    if (!package_.first(record)) return false;
    do {
        SectionView section{};
        if (activeSection_ == noIndex && package_.section(record, section)) {
            activeSection_ = section.sectionIndex;
        }
    } while (package_.next(record));
    if (activeSection_ == noIndex || !package_.first(record)) return false;
    do {
        PageView page{};
        if (package_.page(record, page) && page.sectionIndex == activeSection_) {
            activePage_ = page.pageIndex;
            return true;
        }
    } while (package_.next(record));
    return false;
}

bool DisplayService::pageByIndex(std::uint16_t index, PageView& result) const {
    RecordView record{};
    if (!package_.first(record)) return false;
    do {
        if (package_.page(record, result) && result.pageIndex == index) return true;
    } while (package_.next(record));
    return false;
}

bool DisplayService::sectionByIndex(std::uint16_t index, SectionView& result) const {
    RecordView record{};
    if (!package_.first(record)) return false;
    do {
        if (package_.section(record, result) && result.sectionIndex == index) return true;
    } while (package_.next(record));
    return false;
}

bool DisplayService::switchSection(int direction) {
    const auto count = package_.info().sectionCount;
    if (count == 0u) return false;
    activeSection_ = direction > 0
        ? static_cast<std::uint16_t>((activeSection_ + 1u) % count)
        : static_cast<std::uint16_t>((activeSection_ + count - 1u) % count);
    RecordView record{};
    if (!package_.first(record)) return false;
    do {
        PageView page{};
        if (package_.page(record, page) && page.sectionIndex == activeSection_) {
            activePage_ = page.pageIndex;
            return true;
        }
    } while (package_.next(record));
    return false;
}

bool DisplayService::switchPage(int direction) {
    PageView page{};
    if (!adjacentPage(direction, page)) return false;
    activePage_ = page.pageIndex;
    return true;
}

bool DisplayService::adjacentPage(int direction, PageView& result) const {
    std::uint16_t first{noIndex};
    std::uint16_t previous{noIndex};
    std::uint16_t next{noIndex};
    std::uint16_t last{noIndex};
    std::uint16_t count{};
    bool seenActive{};
    RecordView record{};
    if (!package_.first(record)) return false;
    do {
        PageView page{};
        if (!package_.page(record, page) || page.sectionIndex != activeSection_) continue;
        ++count;
        if (first == noIndex) first = page.pageIndex;
        last = page.pageIndex;
        if (page.pageIndex == activePage_) seenActive = true;
        else if (!seenActive) previous = page.pageIndex;
        else if (next == noIndex) next = page.pageIndex;
    } while (package_.next(record));
    if (count < 2u) return false;
    const auto target = direction > 0 ? (next != noIndex ? next : first)
                                      : (previous != noIndex ? previous : last);
    return pageByIndex(target, result);
}

bool DisplayService::actionAvailable(InputAction action) const {
    if ((action != InputAction::action1 && action != InputAction::action2) ||
        !actionHandler_.execute) return false;
    return !actionHandler_.available || actionHandler_.available(
        actionHandler_.context, action, activeSection_, activePage_);
}

std::string_view DisplayService::actionLabel(InputAction action,
                                             std::string_view configuredLabel) const {
    if (!actionHandler_.label) return configuredLabel;
    const auto label = actionHandler_.label(actionHandler_.context, action,
                                            activeSection_, activePage_);
    return label.empty() ? configuredLabel : label;
}

void DisplayService::handle(InputAction action) {
    if (sleepActive()) return;
    if (modal_.visible) {
        dismissModal();
        return;
    }
    bool changed{};
    switch (action) {
    case InputAction::previousSection: changed = switchSection(-1); break;
    case InputAction::nextSection: changed = switchSection(1); break;
    case InputAction::previousPage: changed = switchPage(-1); break;
    case InputAction::nextPage: changed = switchPage(1); break;
    case InputAction::action1:
    case InputAction::action2:
        if (actionAvailable(action)) {
            changed = actionHandler_.execute(actionHandler_.context, action,
                                             activeSection_, activePage_);
        }
        break;
    default: break;
    }
    if (changed) {
        if (navigationNotification_.changed) {
            navigationNotification_.changed(navigationNotification_.context, activeSection_, activePage_);
        }
        invalidate(policy_.inputSettleMs);
    }
}

void DisplayService::requestFullRefresh() {
    if (sleepActive()) return;
    forceFullRefresh_ = true;
    invalidate(0u);
}

void DisplayService::requestSleep() {
    if (sleepActive()) return;
    sleepRequested_ = true;
    cancelObsoleteTransfer();
}

bool DisplayService::wake() {
    if (sleepRequested_ && !sleeping_) {
        sleepRequested_ = false;
        requestFullRefresh();
        return true;
    }
    if (!sleeping_ || !panel_.init()) return false;
    sleeping_ = false;
    forceFullRefresh_ = true;
    dirty_ = true;
    renderAt_ = runtime_.millis ? runtime_.millis() : 0u;
    clockRefreshAt_ = renderAt_ + policy_.localClockRenderIntervalMs;
    return true;
}

bool DisplayService::pageUsesSource(std::uint16_t sourceIndex) const {
    RecordView record{};
    if (!package_.first(record)) return false;
    do {
        WidgetView widget{};
        if (package_.widget(record, widget) && widget.pageIndex == activePage_ &&
            widget.sourceIndex == sourceIndex) return true;
    } while (package_.next(record));
    return false;
}

bool DisplayService::pageHasLocalClock() const {
    RecordView record{};
    if (!package_.first(record)) return false;
    do {
        WidgetView widget{};
        if (package_.widget(record, widget) && widget.pageIndex == activePage_ &&
            widget.type == WidgetType::localClock) return true;
    } while (package_.next(record));
    return false;
}

void DisplayService::sourceUpdated(std::uint16_t sourceIndex) {
    const auto* value = data_.get(sourceIndex);
    if (value && value->type == ValueType::number) {
        (void)history_.ingest(sourceIndex, value->number, value->updatedAt);
    }
    if (sleepActive()) return;
    if (!pageUsesSource(sourceIndex)) return;
    const auto now = runtime_.millis();
    const auto earliest = lastRenderStartedAt_ + policy_.minimumDataRenderIntervalMs;
    const auto delay = static_cast<std::int32_t>(now - earliest) >= 0 ? 0u : earliest - now;
    invalidate(delay);
}

void DisplayService::showModal(const char* title, const char* message, ModalSeverity severity,
                               bool requiresAcknowledgement) {
    copyText(modal_.title, sizeof modal_.title, title);
    copyText(modal_.message, sizeof modal_.message, message);
    copyText(modal_.prompt, sizeof modal_.prompt,
             requiresAcknowledgement ? "PRESS ANY BUTTON" : nullptr);
    modal_.severity = severity;
    modal_.requiresAcknowledgement = requiresAcknowledgement;
    modal_.visible = true;
    if (notification_.changed) notification_.changed(notification_.context, true, severity);
    invalidate(0u);
}

void DisplayService::dismissModal() {
    if (!modal_.visible) return;
    modal_.visible = false;
    if (notification_.changed) notification_.changed(notification_.context, false, modal_.severity);
    invalidate(0u);
}

void DisplayService::showTransientModal(const char* title, const char* message,
                                        const char* prompt) {
    copyText(transientModal_.title, sizeof transientModal_.title, title);
    copyText(transientModal_.message, sizeof transientModal_.message, message);
    copyText(transientModal_.prompt, sizeof transientModal_.prompt, prompt);
    transientModal_.severity = ModalSeverity::information;
    transientModal_.requiresAcknowledgement = false;
    transientModal_.visible = true;
    invalidate(0u);
}

void DisplayService::dismissTransientModal() {
    if (!transientModal_.visible) return;
    transientModal_.visible = false;
    invalidate(0u);
}

void DisplayService::cancelObsoleteTransfer() {
    if (!activeUpdate_ || !panel_.canCancelDisplay() || !panel_.cancelDisplay()) return;
    activeUpdate_ = false;
    activeFullRefresh_ = false;
}

void DisplayService::invalidate(std::uint32_t delayMs) {
    dirty_ = true;
    const auto now = runtime_.millis ? runtime_.millis() : 0u;
    renderAt_ = now + delayMs;
    cancelObsoleteTransfer();
}

void DisplayService::drawCentered(std::uint16_t x, std::uint16_t width, std::uint16_t y,
                                  const char* text, bool black, std::uint8_t scale) {
    const auto textWidth = Font5x7::textWidth(text, scale);
    const auto textX = textWidth < width ? static_cast<std::uint16_t>(x + (width - textWidth) / 2u) : x;
    Font5x7::drawText(canvas_, textX, y, text, black, scale);
}

void DisplayService::renderWidget(const PageView&, const WidgetView& widget,
                                  std::uint16_t x, std::uint16_t y,
                                  std::uint16_t width, std::uint16_t height) {
    canvas_.drawRect(x, y, width, height, true, 1u);
    char label[48]{};
    copyView(label, sizeof label, widget.label);
    if (*label) drawCentered(x, width, static_cast<std::uint16_t>(y + 5u), label, true, 2u);

    if (widget.type == WidgetType::text) {
        char text[96]{};
        copyView(text, sizeof text, widget.text);
        const auto scale = height >= 100u ? 3u : 2u;
        drawCentered(x, width, static_cast<std::uint16_t>(y + (height - 7u * scale) / 2u),
                     text, true, scale);
        return;
    }

    if (widget.type == WidgetType::localClock) {
        char value[32]{"--:--:--"};
        if (clock_.format) (void)clock_.format(clock_.context, value, sizeof value);
        const auto scale = height >= 100u ? 5u : 3u;
        drawCentered(x, width, static_cast<std::uint16_t>(y + (height - 7u * scale) / 2u),
                     value, true, scale);
        return;
    }

    if (widget.type == WidgetType::unknown) {
        constexpr std::uint8_t informationScale = 2u;
        constexpr std::uint16_t informationHeight = 7u * informationScale;
        constexpr std::uint16_t informationGap = 6u;
        constexpr std::uint16_t blockHeight = 2u * informationHeight + informationGap;
        const auto innerWidth = width > 8u ? static_cast<std::uint16_t>(width - 8u) : width;
        const auto informationY = height > blockHeight
            ? static_cast<std::uint16_t>(y + (height - blockHeight) / 2u)
            : y;
        char message[]{"UNKNOWN WIDGET"};
        fitText(message, sizeof message, innerWidth, informationScale);
        drawCentered(x, width, informationY, message, true, informationScale);
        char typeName[48]{};
        copyView(typeName, sizeof typeName, widget.typeName);
        fitText(typeName, sizeof typeName, innerWidth, informationScale);
        drawCentered(x, width,
                     static_cast<std::uint16_t>(informationY + informationHeight +
                                                informationGap),
                     typeName, true, informationScale);
        return;
    }

    char valueText[48]{"--"};
    char unitText[24]{};
    const auto* value = data_.get(widget.sourceIndex);
    SourceView source{};
    const bool sourceKnown = package_.sourceByIndex(widget.sourceIndex, source);
    const auto now = runtime_.millis ? runtime_.millis() : 0u;
    const bool stale = sourceKnown && source.staleAfterMs != 0u && value &&
        static_cast<std::uint32_t>(now - value->updatedAt) > source.staleAfterMs;
    if (value && !stale && value->type == ValueType::text) {
        const std::string_view text{value->text, value->textLength};
        copyView(valueText, sizeof valueText, text);
    } else if (value && !stale && value->type == ValueType::number) {
        const auto converted = convertUnit(value->number, source.unit, widget.displayUnit);
        (void)formatNumber(converted, static_cast<std::uint8_t>(widget.decimals),
                           valueText, sizeof valueText);
    }
    copyView(unitText, sizeof unitText, widget.displayUnit);
    const auto innerWidth = width > 8u ? static_cast<std::uint16_t>(width - 8u) : width;
    fitText(unitText, sizeof unitText, innerWidth, 2u);
    const auto scale = height >= 150u ? 7u : height >= 90u ? 4u : 2u;
    drawCentered(x, width, static_cast<std::uint16_t>(y + (height - 7u * scale) / 2u),
                 valueText, true, scale);
    if (*unitText) drawCentered(x, width, static_cast<std::uint16_t>(y + height - 18u),
                                unitText, true, 2u);
}

void DisplayService::renderModal(const ModalState& modal) {
    const auto width = static_cast<std::uint16_t>(canvas_.width() > 360u ? 360u : canvas_.width() - 20u);
    const auto height = static_cast<std::uint16_t>(canvas_.height() > 190u ? 190u : canvas_.height() - 20u);
    const auto x = static_cast<std::uint16_t>((canvas_.width() - width) / 2u);
    const auto y = static_cast<std::uint16_t>((canvas_.height() - height) / 2u);
    canvas_.fillRect(x, y, width, height, false);
    canvas_.drawRect(x, y, width, height, true, 3u);
    canvas_.fillRect(static_cast<std::uint16_t>(x + 3u), static_cast<std::uint16_t>(y + 3u),
                     static_cast<std::uint16_t>(width - 6u), 28u, true);
    drawCentered(x, width, static_cast<std::uint16_t>(y + 9u), modal.title, false, 2u);

    constexpr std::uint8_t messageScale = 2u;
    constexpr std::uint16_t messageHeight = 7u * messageScale;
    constexpr std::uint16_t lineAdvance = 18u;
    constexpr std::uint16_t promptAreaHeight = 32u;
    // At scale 2, 28 characters occupy 334 pixels and remain inside the
    // 360-pixel modal with comfortable horizontal padding.
    constexpr std::size_t lineCapacity = 29u;
    const char* cursor = modal.message;
    std::uint16_t lineY = static_cast<std::uint16_t>(y + 42u);
    while (*cursor && lineY + messageHeight <= y + height - promptAreaHeight) {
        char line[lineCapacity]{};
        std::size_t length{};
        std::size_t lastSpace{};
        while (cursor[length] && length + 1u < lineCapacity) {
            if (cursor[length] == ' ') lastSpace = length;
            ++length;
        }
        if (cursor[length] && lastSpace != 0u) length = lastSpace;
        std::memcpy(line, cursor, length);
        line[length] = '\0';
        drawCentered(x, width, lineY, line, true, messageScale);
        cursor += length;
        while (*cursor == ' ') ++cursor;
        lineY = static_cast<std::uint16_t>(lineY + lineAdvance);
    }
    if (*modal.prompt) {
        drawCentered(x, width, static_cast<std::uint16_t>(y + height - 22u),
                     modal.prompt, true, 2u);
    }
}

void DisplayService::render() {
    PageView page{};
    SectionView section{};
    if (!pageByIndex(activePage_, page) || !sectionByIndex(activeSection_, section)) return;
    canvas_.clear(false);
    canvas_.fillRect(0u, 0u, canvas_.width(), headerHeight, true);
    canvas_.fillRect(0u, static_cast<std::uint16_t>(canvas_.height() - footerHeight),
                     canvas_.width(), footerHeight, true);
    char sectionTitle[48]{}, pageTitle[48]{}, heading[100]{};
    copyView(sectionTitle, sizeof sectionTitle, section.title);
    copyView(pageTitle, sizeof pageTitle, page.title);
    std::snprintf(heading, sizeof heading, "%s - %s", sectionTitle, pageTitle);
    drawCentered(horizontalSafeMargin,
                 static_cast<std::uint16_t>(canvas_.width() - 2u * horizontalSafeMargin),
                 7u, heading, false, 2u);

    const auto footerY = static_cast<std::uint16_t>(canvas_.height() - footerHeight + 7u);
    const auto safeWidth = static_cast<std::uint16_t>(canvas_.width() -
                                                       2u * horizontalSafeMargin);
    const bool action1Available = actionAvailable(InputAction::action1);
    const bool action2Available = actionAvailable(InputAction::action2);
    if (section.showButtonLabels || action1Available || action2Available) {
        PageView previousPage{}, nextPage{};
        const auto hasPrevious = adjacentPage(-1, previousPage);
        const auto hasNext = adjacentPage(1, nextPage);
        const std::string_view labels[]{
            hasPrevious ? previousPage.title : std::string_view{},
            hasNext ? nextPage.title : std::string_view{},
            action1Available
                ? actionLabel(InputAction::action1, section.action1Label) : std::string_view{},
            action2Available
                ? actionLabel(InputAction::action2, section.action2Label) : std::string_view{},
        };
        const auto buttonWidth = static_cast<std::uint16_t>(safeWidth / 4u);
        for (std::uint8_t index = 0u; index < 4u; ++index) {
            char label[10]{};
            copyView(label, sizeof label, labels[index]);
            if (labels[index].size() >= sizeof label) label[sizeof label - 2u] = '.';
            const auto x = static_cast<std::uint16_t>(horizontalSafeMargin + index * buttonWidth);
            const auto width = index == 3u
                ? static_cast<std::uint16_t>(safeWidth - 3u * buttonWidth)
                : buttonWidth;
            drawCentered(x, width, footerY, label, false, 2u);
        }
    } else {
        drawCentered(horizontalSafeMargin, safeWidth, footerY,
                     "B2 PREV PAGE   B3 NEXT PAGE", false, 2u);
    }

    const auto contentX = horizontalSafeMargin;
    const auto contentY = static_cast<std::uint16_t>(headerHeight + outerGap);
    const auto contentWidth = safeWidth;
    const auto contentHeight = static_cast<std::uint16_t>(canvas_.height() - headerHeight -
                                                           footerHeight - 2u * outerGap);
    const auto horizontalGaps = static_cast<std::uint16_t>((page.columns - 1u) * page.gap);
    const auto verticalGaps = static_cast<std::uint16_t>((page.rows - 1u) * page.gap);
    const auto cellWidth = static_cast<std::uint16_t>((contentWidth - horizontalGaps) / page.columns);
    const auto cellHeight = static_cast<std::uint16_t>((contentHeight - verticalGaps) / page.rows);

    RecordView record{};
    if (package_.first(record)) do {
        WidgetView widget{};
        if (!package_.widget(record, widget) || widget.pageIndex != activePage_) continue;
        const auto x = static_cast<std::uint16_t>(contentX + widget.column * (cellWidth + page.gap));
        const auto y = static_cast<std::uint16_t>(contentY + widget.row * (cellHeight + page.gap));
        const auto width = static_cast<std::uint16_t>(widget.columnSpan * cellWidth +
                                                       (widget.columnSpan - 1u) * page.gap);
        const auto height = static_cast<std::uint16_t>(widget.rowSpan * cellHeight +
                                                        (widget.rowSpan - 1u) * page.gap);
        renderWidget(page, widget, x, y, width, height);
    } while (package_.next(record));
    if (transientModal_.visible) renderModal(transientModal_);
    else if (modal_.visible) renderModal(modal_);
}

void DisplayService::run() {
    const auto now = runtime_.millis ? runtime_.millis() : 0u;
    if (sleeping_) return;
    if (sleepRequested_) {
        if (activeUpdate_) {
            if (dirty_) cancelObsoleteTransfer();
            if (activeUpdate_) {
                if (panel_.updateState() == UpdateState::prepared) {
                    (void)panel_.commitDisplay();
                } else if (panel_.updateState() == UpdateState::idle) {
                    activeUpdate_ = false;
                    if (activeFullRefresh_) {
                        partialRefreshCount_ = 0u;
                        forceFullRefresh_ = false;
                    } else if (partialRefreshCount_ != UINT32_MAX) {
                        ++partialRefreshCount_;
                    }
                    activeFullRefresh_ = false;
                }
                return;
            }
        }

        const auto state = panel_.updateState();
        if (dirty_ && state == UpdateState::idle) {
            render();
            const auto limit = policy_.fullRefreshAfterPartialUpdates;
            const bool fullRefresh = forceFullRefresh_ ||
                (limit != 0u && partialRefreshCount_ >= limit);
            const auto mode = fullRefresh ? RefreshMode::full : RefreshMode::partial;
            const auto region = canvas_.toRawRegion(
                {0u, 0u, canvas_.width(), canvas_.height()});
            if (panel_.beginDisplay(canvas_.data(), canvas_.size(), mode, region)) {
                dirty_ = false;
                activeUpdate_ = true;
                activeFullRefresh_ = fullRefresh;
                lastRenderStartedAt_ = now;
            }
            return;
        }
        if (state == UpdateState::failed) dirty_ = false;
        if (!dirty_ && (state == UpdateState::idle || state == UpdateState::failed)) {
            panel_.sleep();
            activeUpdate_ = false;
            activeFullRefresh_ = false;
            sleepRequested_ = false;
            sleeping_ = true;
        }
        return;
    }
    if (policy_.localClockRenderIntervalMs != 0u &&
        static_cast<std::int32_t>(now - clockRefreshAt_) >= 0) {
        clockRefreshAt_ = now + policy_.localClockRenderIntervalMs;
        if (!modal_.visible && pageHasLocalClock()) invalidate(0u);
    }
    if (panel_.updateState() == UpdateState::failed) {
        activeUpdate_ = false;
        dirty_ = true;
        if (static_cast<std::int32_t>(now - recoverAt_) < 0) return;
        recoverAt_ = now + 1000u;
        (void)panel_.init();
        return;
    }
    if (activeUpdate_) {
        if (dirty_) cancelObsoleteTransfer();
        if (!activeUpdate_) return;
        if (panel_.updateState() == UpdateState::prepared) {
            (void)panel_.commitDisplay();
        } else if (panel_.updateState() == UpdateState::idle) {
            activeUpdate_ = false;
            if (activeFullRefresh_) {
                partialRefreshCount_ = 0u;
                forceFullRefresh_ = false;
            }
            else if (partialRefreshCount_ != UINT32_MAX) ++partialRefreshCount_;
            activeFullRefresh_ = false;
        }
        return;
    }
    if (!dirty_ || static_cast<std::int32_t>(now - renderAt_) < 0 ||
        panel_.updateState() != UpdateState::idle) return;
    render();
    const auto limit = policy_.fullRefreshAfterPartialUpdates;
    const bool fullRefresh = forceFullRefresh_ ||
        (limit != 0u && partialRefreshCount_ >= limit);
    const auto mode = fullRefresh ? RefreshMode::full : RefreshMode::partial;
    const auto region = canvas_.toRawRegion({0u, 0u, canvas_.width(), canvas_.height()});
    if (!panel_.beginDisplay(canvas_.data(), canvas_.size(), mode, region)) return;
    dirty_ = false;
    activeUpdate_ = true;
    activeFullRefresh_ = fullRefresh;
    lastRenderStartedAt_ = now;
}

} // namespace semantic_display
