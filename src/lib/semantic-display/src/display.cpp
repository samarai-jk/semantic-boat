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
constexpr std::uint16_t sectionIndicatorWidth = 18u;
constexpr std::uint16_t widgetSeparatorWidth = 4u;
constexpr std::uint8_t maximumValueScale = 7u;
constexpr std::uint16_t widgetVerticalPadding = 8u;
constexpr std::uint16_t compactWidgetHeight = 80u;
constexpr std::uint8_t compactTextScaleNumerator = 3u;
constexpr std::uint8_t compactTextScaleDenominator = 2u;
constexpr std::uint16_t compactTextHeight = 11u;
constexpr std::uint16_t regularTextHeight = 14u;
constexpr std::uint16_t auxiliaryTextGap = 4u;

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

void fitCompactText(char* text, std::size_t capacity, std::uint16_t width) {
    if (!text || capacity == 0u) return;
    std::size_t length{};
    while (length + 1u < capacity && text[length]) ++length;
    const bool truncated = Font5x7::textWidthScaled(
        text, compactTextScaleNumerator, compactTextScaleDenominator) > width;
    while (length != 0u && Font5x7::textWidthScaled(
               text, compactTextScaleNumerator, compactTextScaleDenominator) > width) {
        text[--length] = '\0';
    }
    if (truncated && length > 1u) text[length - 1u] = '.';
}

void drawCompactCentered(slstm32::epd::MonochromeCanvas& canvas,
                         std::uint16_t x, std::uint16_t width, std::uint16_t y,
                         const char* text, bool black) {
    const auto textWidth = Font5x7::textWidthScaled(
        text, compactTextScaleNumerator, compactTextScaleDenominator);
    const auto textX = textWidth < width
        ? static_cast<std::uint16_t>(x + (width - textWidth) / 2u) : x;
    Font5x7::drawTextScaled(canvas, textX, y, text, black,
                            compactTextScaleNumerator,
                            compactTextScaleDenominator);
}

std::uint8_t fittedValueScale(std::uint16_t width, std::uint16_t height,
                              std::uint8_t maxDigits) {
    const auto characters = maxDigits != 0u ? maxDigits : defaultValueMaxDigits;
    const auto unscaledWidth = static_cast<std::uint16_t>(characters * 6u - 1u);
    const auto widthScale = static_cast<std::uint16_t>(width / unscaledWidth);
    const auto heightScale = static_cast<std::uint16_t>(height / 7u);
    auto scale = widthScale < heightScale ? widthScale : heightScale;
    if (scale == 0u) scale = 1u;
    return static_cast<std::uint8_t>(
        scale < maximumValueScale ? scale : maximumValueScale);
}

void drawVerticalArrow(slstm32::epd::MonochromeCanvas& canvas,
                       std::uint16_t centerX, std::uint16_t y, bool up) {
    constexpr std::uint16_t halfWidth = 4u;
    constexpr std::uint16_t headHeight = 5u;
    constexpr std::uint16_t stemHeight = 5u;
    if (up) {
        for (std::uint16_t row = 0u; row < headHeight; ++row) {
            canvas.fillRect(static_cast<std::uint16_t>(centerX - row),
                            static_cast<std::uint16_t>(y + row),
                            static_cast<std::uint16_t>(2u * row + 1u), 1u, false);
        }
        canvas.fillRect(static_cast<std::uint16_t>(centerX - 1u),
                        static_cast<std::uint16_t>(y + headHeight),
                        3u, stemHeight, false);
        return;
    }
    canvas.fillRect(static_cast<std::uint16_t>(centerX - 1u), y,
                    3u, stemHeight, false);
    for (std::uint16_t row = 0u; row < headHeight; ++row) {
        const auto inset = static_cast<std::uint16_t>(halfWidth - row);
        canvas.fillRect(static_cast<std::uint16_t>(centerX - inset),
                        static_cast<std::uint16_t>(y + stemHeight + row),
                        static_cast<std::uint16_t>(2u * inset + 1u), 1u, false);
    }
}

} // namespace

bool DisplayService::setPackage(PackageView package) {
    if (!package.valid()) return false;
    package_ = package;
    configured_ = true;
    if (!initialized_) return true;
    cancelObsoleteTransfer();
    if (!data_.configure(package_) ||
        !history_.configure(package_, runtime_.millis ? runtime_.millis() : 0u) ||
        !selectFirstPage()) {
        configured_ = false;
        return false;
    }
    if (navigationNotification_.changed) {
        navigationNotification_.changed(navigationNotification_.context,
                                        activeSection_, activePage_);
    }
    partialRefreshCount_ = 0u;
    forceFullRefresh_ = true;
    clockRefreshAt_ = (runtime_.millis ? runtime_.millis() : 0u) +
        policy_.localClockRenderIntervalMs;
    invalidate(0u);
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
    initialized_ = panel_.display(canvas_.data(), canvas_.size(), RefreshMode::full);
    return initialized_;
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
    if (count < 2u) return false;
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

bool DisplayService::activePageIsFirstInSection() const {
    RecordView record{};
    if (!package_.first(record)) return false;
    do {
        PageView page{};
        if (package_.page(record, page) && page.sectionIndex == activeSection_) {
            return page.pageIndex == activePage_;
        }
    } while (package_.next(record));
    return false;
}

void DisplayService::logicalSectionAvailability(bool& previous, bool& next) const {
    previous = false;
    next = false;
    bool seenActive{};
    RecordView record{};
    if (!package_.first(record)) return;
    do {
        SectionView section{};
        if (!package_.section(record, section)) continue;
        if (section.sectionIndex == activeSection_) {
            seenActive = true;
        } else if (seenActive) {
            next = true;
        } else {
            previous = true;
        }
    } while (package_.next(record));
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
    if (alert_.visible) {
        AlertAction selected{};
        bool valid{true};
        switch (action) {
        case InputAction::previousPage: selected = AlertAction::acknowledge; break;
        case InputAction::nextPage: selected = AlertAction::snooze; break;
        case InputAction::action1: selected = AlertAction::hideHere; break;
        case InputAction::action2: selected = AlertAction::silence; break;
        default: valid = false; break;
        }
        if (valid && alert_.handler.selected) {
            alert_.handler.selected(alert_.handler.context, selected);
        }
        return;
    }
    if (selection_.visible) {
        const auto count = selection_.list.count;
        if (count == 0u) {
            dismissSelection();
            return;
        }
        const auto complete = [this](SelectionResult result) {
            const auto list = selection_.list;
            const auto selected = selection_.selected;
            selection_.visible = false;
            invalidate(0u);
            if (list.completed) {
                list.completed(list.context, result, selected);
            }
        };
        if (action == InputAction::previousSection) {
            if (selection_.selected != 0u) --selection_.selected;
            invalidate(0u);
        } else if (action == InputAction::nextSection) {
            if (static_cast<std::size_t>(selection_.selected) + 1u < count) {
                ++selection_.selected;
            }
            invalidate(0u);
        } else if (action == InputAction::previousPage) {
            complete(SelectionResult::cancelled);
        } else if (action == InputAction::nextPage) {
            complete(SelectionResult::selected);
        } else if (action == InputAction::action1 || action == InputAction::action2) {
            complete(action == InputAction::action2
                ? SelectionResult::selected : SelectionResult::cancelled);
        }
        return;
    }
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

void DisplayService::sourceUpdated(std::uint16_t sourceIndex, bool requestRender) {
    const auto* value = data_.get(sourceIndex);
    if (value && value->type == ValueType::number) {
        (void)history_.ingest(sourceIndex, value->number, value->updatedAt);
    }
    if (!requestRender) return;
    // Continue updating the data store and histories while an overlay is up,
    // but do not let high-rate values compete with dialog interaction for the
    // EPD. Dismissing every dialog invalidates the complete normal screen.
    if (sleepActive() || dialogVisible() || renderingPaused_) return;
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
    modalPresentationPending_ = true;
    if (notification_.changed) notification_.changed(notification_.context, true, severity);
    invalidate(0u);
}

void DisplayService::dismissModal() {
    if (!modal_.visible) return;
    modal_.visible = false;
    modalPresentationPending_ = false;
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

bool DisplayService::showSelection(const char* title, SelectionList list,
                                   std::size_t selectedIndex) {
    if (list.count == 0u || list.count > UINT16_MAX || !list.item) return false;
    copyText(selection_.title, sizeof selection_.title, title);
    selection_.list = list;
    selection_.selected = static_cast<std::uint16_t>(
        selectedIndex < list.count ? selectedIndex : 0u);
    selection_.visible = true;
    invalidate(0u);
    return true;
}

void DisplayService::dismissSelection() {
    if (!selection_.visible) return;
    selection_.visible = false;
    invalidate(0u);
}

void DisplayService::showAlert(const char* title, const char* message, AlertLevel level,
                               std::uint8_t position, std::uint8_t count,
                               std::uint16_t overflowCount, AlertHandler handler) {
    copyText(alert_.title, sizeof alert_.title, title);
    copyText(alert_.message, sizeof alert_.message, message);
    alert_.level = level;
    alert_.position = position;
    alert_.count = count;
    alert_.overflowCount = overflowCount;
    alert_.handler = handler;
    alert_.visible = true;
    alertPresentationPending_ = true;
    invalidate(0u);
}

void DisplayService::updateAlertQueue(std::uint8_t count,
                                      std::uint16_t overflowCount) {
    if (!alert_.visible ||
        (alert_.count == count && alert_.overflowCount == overflowCount)) return;
    alert_.count = count;
    alert_.overflowCount = overflowCount;
    // This is only a header update. Do not mark the alert as newly presented:
    // doing so would replay warning cues or restart an active alarm cadence.
    invalidate(0u);
}

void DisplayService::dismissAlert() {
    if (!alert_.visible) return;
    alert_.visible = false;
    alertPresentationPending_ = false;
    invalidate(0u);
}

void DisplayService::setRenderingPaused(bool paused) {
    if (renderingPaused_ == paused) return;
    renderingPaused_ = paused;
    if (paused) {
        cancelObsoleteTransfer();
    } else {
        // Render the entire current page from the latest data accumulated
        // while rendering was suspended.
        invalidate(0u);
    }
}

bool DisplayService::dialogVisible() const {
    return alert_.visible || modal_.visible || transientModal_.visible || selection_.visible;
}

void DisplayService::cancelObsoleteTransfer() {
    if (!activeUpdate_ || !panel_.canCancelDisplay() || !panel_.cancelDisplay()) return;
    activeUpdate_ = false;
    activeFullRefresh_ = false;
    activeModalPresentation_ = false;
    activeAlertPresentation_ = false;
}

void DisplayService::finishActiveUpdate() {
    activeUpdate_ = false;
    if (activeFullRefresh_) {
        partialRefreshCount_ = 0u;
        forceFullRefresh_ = false;
    } else if (partialRefreshCount_ != UINT32_MAX) {
        ++partialRefreshCount_;
    }
    activeFullRefresh_ = false;

    if (activeModalPresentation_) {
        activeModalPresentation_ = false;
        // A newer invalidation means this transfer did not present the current
        // modal contents. Keep the notification pending for the replacement frame.
        if (!dirty_ && modal_.visible) {
            modalPresentationPending_ = false;
            if (notification_.presented) {
                notification_.presented(notification_.context, modal_.severity);
            }
        }
    }

    if (activeAlertPresentation_) {
        activeAlertPresentation_ = false;
        if (!dirty_ && alert_.visible) {
            alertPresentationPending_ = false;
            if (alert_.handler.presented) alert_.handler.presented(alert_.handler.context);
        }
    }
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
    const bool compact = height < compactWidgetHeight;
    const auto auxiliaryTextHeight = compact ? compactTextHeight : regularTextHeight;
    const auto innerWidth = width > 8u ? static_cast<std::uint16_t>(width - 8u) : width;
    char label[48]{};
    copyView(label, sizeof label, widget.label);
    if (*label) {
        if (compact) {
            fitCompactText(label, sizeof label, innerWidth);
            drawCompactCentered(canvas_, x, width,
                                static_cast<std::uint16_t>(y + widgetVerticalPadding),
                                label, true);
        } else {
            fitText(label, sizeof label, innerWidth, 2u);
            drawCentered(x, width,
                         static_cast<std::uint16_t>(y + widgetVerticalPadding),
                         label, true, 2u);
        }
    }

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
    if (compact) fitCompactText(unitText, sizeof unitText, innerWidth);
    else fitText(unitText, sizeof unitText, innerWidth, 2u);
    const auto valueTop = static_cast<std::uint16_t>(y + widgetVerticalPadding +
        (*label ? auxiliaryTextHeight + auxiliaryTextGap : 0u));
    const auto valueBottomInset = static_cast<std::uint16_t>(widgetVerticalPadding +
        (*unitText ? auxiliaryTextHeight + auxiliaryTextGap : 0u));
    const auto valueBottom = height > valueBottomInset
        ? static_cast<std::uint16_t>(y + height - valueBottomInset) : valueTop;
    const auto valueHeight = valueBottom > valueTop
        ? static_cast<std::uint16_t>(valueBottom - valueTop) : 7u;
    const auto scale = fittedValueScale(innerWidth, valueHeight, widget.maxDigits);
    fitText(valueText, sizeof valueText, innerWidth, scale);
    const auto valueY = valueHeight > 7u * scale
        ? static_cast<std::uint16_t>(valueTop + (valueHeight - 7u * scale) / 2u)
        : valueTop;
    drawCentered(x, width, valueY, valueText, true, scale);
    if (*unitText) {
        const auto unitY = static_cast<std::uint16_t>(
            y + height - widgetVerticalPadding - auxiliaryTextHeight);
        if (compact) drawCompactCentered(canvas_, x, width, unitY, unitText, true);
        else drawCentered(x, width, unitY, unitText, true, 2u);
    }
}

void DisplayService::renderWidgetSeparators(const PageView& page,
                                             std::uint16_t contentX,
                                             std::uint16_t contentY,
                                             std::uint16_t cellWidth,
                                             std::uint16_t cellHeight,
                                             std::uint16_t gap) {
    const auto gridX = [=](std::uint16_t column) {
        return static_cast<std::uint16_t>(contentX + column * (cellWidth + gap));
    };
    const auto gridY = [=](std::uint16_t row) {
        return static_cast<std::uint16_t>(contentY + row * (cellHeight + gap));
    };
    const auto spanSize = [](std::uint16_t cells, std::uint16_t cellSize,
                             std::uint16_t cellGap) {
        return static_cast<std::uint16_t>(cells * cellSize +
                                          (cells - 1u) * cellGap);
    };
    const auto separatorInset = static_cast<std::uint16_t>(
        (gap - widgetSeparatorWidth) / 2u);

    RecordView leftRecord{};
    if (!package_.first(leftRecord)) return;
    do {
        WidgetView first{};
        if (!package_.widget(leftRecord, first) || first.pageIndex != page.pageIndex) continue;
        const auto firstRight = static_cast<std::uint16_t>(first.column + first.columnSpan);
        const auto firstBottom = static_cast<std::uint16_t>(first.row + first.rowSpan);

        RecordView rightRecord{};
        if (!package_.first(rightRecord)) return;
        do {
            WidgetView second{};
            if (!package_.widget(rightRecord, second) ||
                second.pageIndex != page.pageIndex) continue;

            if (firstRight == second.column) {
                const auto start = first.row > second.row ? first.row : second.row;
                const auto firstEnd = static_cast<std::uint16_t>(first.row + first.rowSpan);
                const auto secondEnd = static_cast<std::uint16_t>(second.row + second.rowSpan);
                const auto end = firstEnd < secondEnd ? firstEnd : secondEnd;
                if (start < end) {
                    canvas_.fillRect(static_cast<std::uint16_t>(
                                         gridX(firstRight) - gap + separatorInset),
                                     gridY(start), widgetSeparatorWidth,
                                     spanSize(static_cast<std::uint16_t>(end - start),
                                              cellHeight, gap), true);
                }
            }

            if (firstBottom == second.row) {
                const auto start = first.column > second.column
                    ? first.column : second.column;
                const auto firstEnd = static_cast<std::uint16_t>(
                    first.column + first.columnSpan);
                const auto secondEnd = static_cast<std::uint16_t>(
                    second.column + second.columnSpan);
                const auto end = firstEnd < secondEnd ? firstEnd : secondEnd;
                if (start < end) {
                    canvas_.fillRect(gridX(start),
                                     static_cast<std::uint16_t>(
                                         gridY(firstBottom) - gap + separatorInset),
                                     spanSize(static_cast<std::uint16_t>(end - start),
                                              cellWidth, gap),
                                     widgetSeparatorWidth, true);
                }
            }
        } while (package_.next(rightRecord));
    } while (package_.next(leftRecord));
}

void DisplayService::renderModal(const ModalState& modal) {
    const auto width = static_cast<std::uint16_t>(canvas_.width() > 360u ? 360u : canvas_.width() - 20u);
    const auto height = static_cast<std::uint16_t>(canvas_.height() > 190u ? 190u : canvas_.height() - 20u);
    const auto x = static_cast<std::uint16_t>((canvas_.width() - width) / 2u);
    const auto y = static_cast<std::uint16_t>((canvas_.height() - height) / 2u);
    // render() already cleared the full dialog screen. Avoid repainting this
    // large white rectangle pixel-by-pixel on the MCU.
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

void DisplayService::renderSelection() {
    const auto width = static_cast<std::uint16_t>(
        canvas_.width() > 420u ? 420u : canvas_.width() - 20u);
    const auto height = static_cast<std::uint16_t>(
        canvas_.height() > 240u ? 240u : canvas_.height() - 20u);
    const auto x = static_cast<std::uint16_t>((canvas_.width() - width) / 2u);
    const auto y = static_cast<std::uint16_t>((canvas_.height() - height) / 2u);
    constexpr std::uint16_t header = 36u;
    constexpr std::uint16_t footer = 31u;
    constexpr std::uint16_t rowHeight = 30u;
    const auto visibleRows = static_cast<std::uint16_t>((height - header - footer) / rowHeight);
    const auto selected = static_cast<std::size_t>(selection_.selected);
    const auto first = selected >= visibleRows
        ? selected - visibleRows + 1u : 0u;

    // render() already cleared the full dialog screen. Avoid repainting this
    // large white rectangle pixel-by-pixel on every selection movement.
    canvas_.drawRect(x, y, width, height, true, 3u);
    canvas_.fillRect(static_cast<std::uint16_t>(x + 3u),
                     static_cast<std::uint16_t>(y + 3u),
                     static_cast<std::uint16_t>(width - 6u), 28u, true);
    drawCentered(x, width, static_cast<std::uint16_t>(y + 9u),
                 selection_.title, false, 2u);

    for (std::uint16_t row = 0u; row < visibleRows; ++row) {
        const auto index = first + row;
        if (index >= selection_.list.count) break;
        const auto rowY = static_cast<std::uint16_t>(y + header + row * rowHeight);
        const bool active = index == selected;
        if (active) {
            canvas_.fillRect(static_cast<std::uint16_t>(x + 10u),
                             static_cast<std::uint16_t>(rowY + 3u),
                             static_cast<std::uint16_t>(width - 20u),
                             static_cast<std::uint16_t>(rowHeight - 6u), true);
        }
        char label[48]{};
        copyView(label, sizeof label,
                 selection_.list.item(selection_.list.context, index));
        fitText(label, sizeof label, static_cast<std::uint16_t>(width - 28u), 2u);
        drawCentered(static_cast<std::uint16_t>(x + 14u),
                     static_cast<std::uint16_t>(width - 28u),
                     static_cast<std::uint16_t>(rowY + 8u), label, !active, 2u);
    }

    const char* labels[]{"", "", "CANCEL", "SELECT"};
    const auto footerWidth = static_cast<std::uint16_t>((width - 16u) / 4u);
    for (std::uint8_t index = 0u; index < 4u; ++index) {
        drawCentered(static_cast<std::uint16_t>(x + 8u + index * footerWidth),
                     footerWidth, static_cast<std::uint16_t>(y + height - 22u),
                     labels[index], true, 2u);
    }
}

void DisplayService::renderAlert() {
    constexpr const char* levelNames[]{"SYSTEM INFO", "SYSTEM WARNING",
                                        "SYSTEM ALARM", "EMERGENCY"};
    const auto levelIndex = static_cast<std::size_t>(alert_.level);
    const auto* levelName = levelIndex < 4u ? levelNames[levelIndex] : levelNames[0];
    constexpr std::uint16_t header = 42u;
    constexpr std::uint16_t footer = 42u;
    constexpr std::uint16_t margin = 10u;

    canvas_.clear(false);
    canvas_.fillRect(0u, 0u, canvas_.width(), header, true);
    char alarmCount[24]{};
    const bool multipleAlarms = alert_.count > 1u || alert_.overflowCount != 0u;
    std::uint16_t headingWidth = canvas_.width();
    if (multipleAlarms) {
        if (alert_.overflowCount != 0u) {
            std::snprintf(alarmCount, sizeof alarmCount, "%u+ ALARMS",
                          static_cast<unsigned>(alert_.count));
        } else {
            std::snprintf(alarmCount, sizeof alarmCount, "%u ALARMS",
                          static_cast<unsigned>(alert_.count));
        }
        constexpr std::uint8_t countScale = 2u;
        const auto countWidth = Font5x7::textWidth(alarmCount, countScale);
        constexpr std::uint16_t countRightMargin = 8u;
        Font5x7::drawText(canvas_, static_cast<std::uint16_t>(canvas_.width() -
                             countWidth - countRightMargin), 13u,
                         alarmCount, false, countScale);
        headingWidth = static_cast<std::uint16_t>(canvas_.width() -
            countWidth - 2u * countRightMargin);
    }
    drawCentered(0u, headingWidth, 13u, levelName, false, 2u);

    const bool urgent = alert_.level != AlertLevel::info;
    const auto textX = static_cast<std::uint16_t>(urgent ? 78u : margin);
    const auto textWidth = static_cast<std::uint16_t>(canvas_.width() - textX - margin);
    if (urgent) {
        Font5x7::drawText(canvas_, 23u, 86u, "!", true, 10u);
    }
    char title[48]{};
    copyText(title, sizeof title, alert_.title);
    fitText(title, sizeof title, textWidth, 3u);
    drawCentered(textX, textWidth, 58u, title, true, 3u);

    constexpr std::uint8_t messageScale = 2u;
    constexpr std::uint16_t lineAdvance = 20u;
    constexpr std::size_t lineCapacity = 31u;
    const auto contentBottom = static_cast<std::uint16_t>(canvas_.height() - footer);
    const char* cursor = alert_.message;
    std::uint16_t lineY = 94u;
    while (*cursor && static_cast<std::uint16_t>(lineY + 14u) < contentBottom) {
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
        fitText(line, sizeof line, textWidth, messageScale);
        drawCentered(textX, textWidth, lineY, line, true, messageScale);
        cursor += length;
        while (*cursor == ' ') ++cursor;
        lineY = static_cast<std::uint16_t>(lineY + lineAdvance);
    }

    constexpr const char* labels[]{"ACK", "SNOOZE", "HIDE", "SILENT"};
    const auto buttonWidth = static_cast<std::uint16_t>(canvas_.width() / 4u);
    const auto footerY = static_cast<std::uint16_t>(canvas_.height() - footer);
    canvas_.fillRect(0u, footerY, canvas_.width(), footer, true);
    for (std::uint8_t index = 0u; index < 4u; ++index) {
        if (index != 0u) {
            canvas_.fillRect(static_cast<std::uint16_t>(index * buttonWidth), footerY,
                             1u, footer, false);
        }
        drawCentered(static_cast<std::uint16_t>(index * buttonWidth), buttonWidth,
                     static_cast<std::uint16_t>(footerY + 14u), labels[index], false, 2u);
    }
}

void DisplayService::render() {
    if (alert_.visible) {
        renderAlert();
        return;
    }
    // Dialogs are self-contained screens. Rebuilding the application page
    // underneath them is both invisible and surprisingly expensive for large
    // packages: every widget/source lookup and separator scan used to run on
    // each menu movement. Apart from wasting CPU, that delayed the cooperative
    // tone driver and stretched a 40 ms key click into an audible long beep.
    if (selection_.visible) {
        canvas_.clear(false);
        renderSelection();
        return;
    }
    if (transientModal_.visible) {
        canvas_.clear(false);
        renderModal(transientModal_);
        return;
    }
    if (modal_.visible) {
        canvas_.clear(false);
        renderModal(modal_);
        return;
    }
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
    bool previousSectionAvailable{}, nextSectionAvailable{};
    logicalSectionAvailability(previousSectionAvailable, nextSectionAvailable);
    const auto indicatorWidth = package_.info().sectionCount > 1u
        ? sectionIndicatorWidth : 0u;
    const auto safeWidth = static_cast<std::uint16_t>(canvas_.width() -
                                                       2u * horizontalSafeMargin);
    const auto statusWidth = static_cast<std::uint16_t>(safeWidth - indicatorWidth);
    drawCentered(horizontalSafeMargin,
                 statusWidth, 7u, heading, false, 2u);

    const auto footerY = static_cast<std::uint16_t>(canvas_.height() - footerHeight + 7u);
    const bool action1Available = actionAvailable(InputAction::action1);
    const bool action2Available = actionAvailable(InputAction::action2);
    if (section.showButtonLabels || action1Available || action2Available) {
        PageView previousPage{}, nextPage{};
        const auto hasPrevious = adjacentPage(-1, previousPage);
        const auto hasNext = adjacentPage(1, nextPage);
        bool showPrevious = hasPrevious;
        bool showNext = hasNext;
        if (hasPrevious && hasNext && previousPage.pageIndex == nextPage.pageIndex) {
            // With exactly two pages both wrapped directions target the same page.
            // Label only the direction that follows the visible page order.
            showNext = activePageIsFirstInSection();
            showPrevious = !showNext;
        }
        const std::string_view labels[]{
            showPrevious ? previousPage.title : std::string_view{},
            showNext ? nextPage.title : std::string_view{},
            action1Available
                ? actionLabel(InputAction::action1, section.action1Label) : std::string_view{},
            action2Available
                ? actionLabel(InputAction::action2, section.action2Label) : std::string_view{},
        };
        const auto buttonWidth = static_cast<std::uint16_t>(statusWidth / 4u);
        for (std::uint8_t index = 0u; index < 4u; ++index) {
            char label[10]{};
            copyView(label, sizeof label, labels[index]);
            if (labels[index].size() >= sizeof label) label[sizeof label - 2u] = '.';
            const auto x = static_cast<std::uint16_t>(horizontalSafeMargin + index * buttonWidth);
            const auto width = index == 3u
                ? static_cast<std::uint16_t>(statusWidth - 3u * buttonWidth)
                : buttonWidth;
            drawCentered(x, width, footerY, label, false, 2u);
        }
    } else {
        drawCentered(horizontalSafeMargin, statusWidth, footerY,
                     "B2 PREV PAGE   B3 NEXT PAGE", false, 2u);
    }

    const auto indicatorX = static_cast<std::uint16_t>(
        canvas_.width() - horizontalSafeMargin - sectionIndicatorWidth / 2u);
    if (previousSectionAvailable) drawVerticalArrow(canvas_, indicatorX, 10u, true);
    if (nextSectionAvailable) {
        drawVerticalArrow(canvas_, indicatorX,
                          static_cast<std::uint16_t>(canvas_.height() - footerHeight + 9u),
                          false);
    }

    const auto contentX = horizontalSafeMargin;
    const auto contentY = static_cast<std::uint16_t>(headerHeight + outerGap);
    const auto contentWidth = safeWidth;
    const auto contentHeight = static_cast<std::uint16_t>(canvas_.height() - headerHeight -
                                                           footerHeight - 2u * outerGap);
    const auto gap = page.gap < widgetSeparatorWidth ? widgetSeparatorWidth : page.gap;
    const auto horizontalGaps = static_cast<std::uint16_t>((page.columns - 1u) * gap);
    const auto verticalGaps = static_cast<std::uint16_t>((page.rows - 1u) * gap);
    const auto cellWidth = static_cast<std::uint16_t>((contentWidth - horizontalGaps) / page.columns);
    const auto cellHeight = static_cast<std::uint16_t>((contentHeight - verticalGaps) / page.rows);

    RecordView record{};
    if (package_.first(record)) do {
        WidgetView widget{};
        if (!package_.widget(record, widget) || widget.pageIndex != activePage_) continue;
        const auto x = static_cast<std::uint16_t>(contentX + widget.column * (cellWidth + gap));
        const auto y = static_cast<std::uint16_t>(contentY + widget.row * (cellHeight + gap));
        const auto width = static_cast<std::uint16_t>(widget.columnSpan * cellWidth +
                                                       (widget.columnSpan - 1u) * gap);
        const auto height = static_cast<std::uint16_t>(widget.rowSpan * cellHeight +
                                                        (widget.rowSpan - 1u) * gap);
        renderWidget(page, widget, x, y, width, height);
    } while (package_.next(record));
    renderWidgetSeparators(page, contentX, contentY, cellWidth, cellHeight, gap);
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
                    finishActiveUpdate();
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
                activeAlertPresentation_ = alertPresentationPending_ && alert_.visible;
                activeModalPresentation_ = !activeAlertPresentation_ &&
                    modalPresentationPending_ && modal_.visible;
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
        if (!dialogVisible() && !renderingPaused_ && pageHasLocalClock()) invalidate(0u);
    }
    if (panel_.updateState() == UpdateState::failed) {
        activeUpdate_ = false;
        activeModalPresentation_ = false;
        activeAlertPresentation_ = false;
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
            finishActiveUpdate();
        }
        return;
    }
    // Safety alerts may pre-empt a configuration transfer's normal-render
    // pause. The accumulated application page stays dirty until the alert is
    // dismissed and ordinary rendering resumes.
    if (renderingPaused_ && !alert_.visible) return;
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
    activeAlertPresentation_ = alertPresentationPending_ && alert_.visible;
    activeModalPresentation_ = !activeAlertPresentation_ &&
        modalPresentationPending_ && modal_.visible;
    lastRenderStartedAt_ = now;
}

} // namespace semantic_display
