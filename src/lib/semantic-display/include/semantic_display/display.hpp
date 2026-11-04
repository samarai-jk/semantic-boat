#pragma once

#include <cstddef>
#include <cstdint>
#include "semantic_display/alert.hpp"
#include "semantic_display/data.hpp"
#include "semantic_display/history.hpp"
#include "semantic_display/package.hpp"
#include "slstm32/epd/monochrome_canvas.hpp"
#include "slstm32/epd/transport.hpp"
#include "slstm32/runtime.hpp"
#include "slstm32/service.hpp"

namespace semantic_display {

enum class InputAction : std::uint8_t {
    previousSection,
    nextSection,
    previousPage,
    nextPage,
    action1,
    action2,
};

enum class ModalSeverity : std::uint8_t { information, warning, error, alarm };

enum class SelectionResult : std::uint8_t { selected, cancelled };

struct SelectionList {
    void* context{};
    std::size_t count{};
    std::string_view (*item)(void* context, std::size_t index){};
    void (*completed)(void* context, SelectionResult result, std::size_t index){};
};

struct LocalClock {
    void* context{};
    bool (*format)(void* context, char* output, std::size_t capacity){};
};

struct HeaderStatus {
    void* context{};
    std::string_view (*text)(void* context){};
};

struct DisplayPolicy {
    std::uint32_t inputSettleMs{100u};
    std::uint32_t minimumDataRenderIntervalMs{333u};
    std::uint32_t fullRefreshAfterPartialUpdates{};
    std::uint32_t localClockRenderIntervalMs{60000u};
};

struct ModalNotification {
    void* context{};
    void (*changed)(void* context, bool visible, ModalSeverity severity){};
    void (*presented)(void* context, ModalSeverity severity){};
};

struct NavigationNotification {
    void* context{};
    void (*changed)(void* context, std::uint16_t sectionIndex, std::uint16_t pageIndex){};
};

struct ActionHandler {
    void* context{};
    bool (*available)(void* context, InputAction action, std::uint16_t sectionIndex,
                      std::uint16_t pageIndex){};
    bool (*execute)(void* context, InputAction action, std::uint16_t sectionIndex,
                    std::uint16_t pageIndex){};
    std::string_view (*label)(void* context, InputAction action, std::uint16_t sectionIndex,
                              std::uint16_t pageIndex){};
};

// Graph time frames (15 minutes .. 1 year): a frame spreads a plot's history
// points over its span, so it sets the sample interval of the plot's sources.
inline constexpr std::uint8_t timeFrameCount = 13u;
std::uint32_t timeFrameSeconds(std::uint8_t frame);
const char* timeFrameLabel(std::uint8_t frame);

struct HistoryHandler {
    void* context{};
    // The user chose a time frame for the plot of a page; its sources have
    // the new interval and an empty ring (load their history now).
    void (*timeFrameChanged)(void* context, std::uint16_t pageIndex, std::uint8_t frame){};
};

struct AlertHandler {
    void* context{};
    void (*selected)(void* context, AlertAction action){};
    void (*presented)(void* context){};
};

class DisplayService final : public slstm32::Service {
public:
    DisplayService(slstm32::Runtime runtime, slstm32::epd::AsyncPanel& panel,
                   slstm32::epd::MonochromeCanvas& canvas, DataStore& data,
                   HistoryStore& history, LocalClock clock = {},
                   DisplayPolicy policy = {}, ModalNotification notification = {},
                   NavigationNotification navigationNotification = {},
                   ActionHandler actionHandler = {}, HeaderStatus headerStatus = {})
        : runtime_(runtime), panel_(panel), canvas_(canvas), data_(data), history_(history),
          clock_(clock), policy_(policy), notification_(notification),
          navigationNotification_(navigationNotification), actionHandler_(actionHandler),
          headerStatus_(headerStatus) {}

    bool setPackage(PackageView package);
    // The page init() shows instead of the first one (a view restored after
    // a restart); ignored if the package has no such page.
    void setStartPage(std::uint16_t pageIndex, bool graph) { startPage_ = pageIndex; startGraph_ = graph; }
    bool init() override;
    void run() override;
    void handle(InputAction action);
    void sourceUpdated(std::uint16_t sourceIndex, bool requestRender = true);
    void requestFullRefresh();
    void requestSleep();
    void requestShutdown();
    bool wake();
    void showModal(const char* title, const char* message, ModalSeverity severity,
                   bool requiresAcknowledgement = true);
    void dismissModal();
    void showTransientModal(const char* title, const char* message, const char* prompt = nullptr);
    void dismissTransientModal();
    bool showSelection(const char* title, SelectionList list, std::size_t selectedIndex = 0u);
    void dismissSelection();
    // Page menu: the sections as tiles, then the pages of the chosen section.
    // Previous/next section move the cursor up/down, previous/next page
    // left/right, action 1 goes back, action 2 selects. A one-page section
    // opens directly.
    bool showPageMenu();
    void dismissPageMenu();
    bool pageMenuVisible() const { return pageMenu_.visible; }
    void showAlert(const char* title, const char* message, AlertLevel level,
                   std::uint8_t position, std::uint8_t count,
                   std::uint16_t overflowCount, AlertHandler handler);
    void updateAlertQueue(std::uint8_t count, std::uint16_t overflowCount);
    void dismissAlert();
    void setRenderingPaused(bool paused);
    void setHeaderStatus(HeaderStatus status) { headerStatus_ = status; }
    void setHistoryHandler(HistoryHandler handler) { historyHandler_ = handler; }
    // The time frame of a page's plot (the nearest to its sources' interval),
    // and setting it (restoring a stored choice; no handler call).
    std::uint8_t timeFrame(std::uint16_t pageIndex) const;
    bool applyTimeFrame(std::uint16_t pageIndex, std::uint8_t frame);
    // History of a source was loaded: redraw a graph showing it.
    void historyChanged(std::uint16_t sourceIndex);

    std::uint16_t activeSection() const { return activeSection_; }
    std::uint16_t activePage() const { return activePage_; }
    bool modalVisible() const {
        return modal_.visible || transientModal_.visible || selection_.visible || pageMenu_.visible;
    }
    bool selectionVisible() const { return selection_.visible; }
    bool alertVisible() const { return alert_.visible; }
    bool sleeping() const { return sleeping_; }
    // A change waits to be drawn or the panel is still updating.
    bool updatePending() const { return dirty_ || activeUpdate_; }
    bool sleepActive() const { return sleeping_ || sleepRequested_; }
    std::uint32_t partialRefreshCount() const { return partialRefreshCount_; }
    bool graphMode() const { return graphMode_; }

private:
    struct ModalState {
        char title[32]{};
        char message[128]{};
        char prompt[48]{};
        ModalSeverity severity{ModalSeverity::information};
        bool visible{};
        bool requiresAcknowledgement{};
    };

    struct SelectionState {
        char title[32]{};
        SelectionList list{};
        std::uint16_t selected{};
        bool visible{};
    };

    struct AlertState {
        char title[48]{};
        char message[128]{};
        AlertLevel level{AlertLevel::info};
        std::uint8_t position{};
        std::uint8_t count{};
        std::uint16_t overflowCount{};
        AlertHandler handler{};
        bool visible{};
    };

    void invalidate(std::uint32_t delayMs);
    void cancelObsoleteTransfer();
    bool selectFirstPage();
    bool switchSection(int direction);
    bool switchPage(int direction);
    bool adjacentPage(int direction, PageView& page) const;
    bool activePageIsFirstInSection() const;
    void logicalSectionAvailability(bool& previous, bool& next) const;
    bool actionAvailable(InputAction action) const;
    std::string_view actionLabel(InputAction action, std::string_view configuredLabel) const;
    bool dialogVisible() const;
    bool pageUsesSource(std::uint16_t sourceIndex) const;
    bool pageHasLocalClock() const;
    bool pageByIndex(std::uint16_t index, PageView& page) const;
    bool sectionByIndex(std::uint16_t index, SectionView& section) const;
    void render();
    void renderHistoryPlot(const HistoryPlotView& plot, std::uint16_t x, std::uint16_t y,
                           std::uint16_t width, std::uint16_t height);
    void renderWidget(const PageView& page, const WidgetView& widget,
                      std::uint16_t x, std::uint16_t y,
                      std::uint16_t width, std::uint16_t height);
    void renderWidgetSeparators(const PageView& page, std::uint16_t contentX,
                                std::uint16_t contentY, std::uint16_t cellWidth,
                                std::uint16_t cellHeight, std::uint16_t gap);
    struct PageMenuState {
        std::uint16_t section{noIndex}; // noIndex: choosing a section
        std::uint16_t cursor{};
        bool visible{};
        bool timeFrames{}; // choosing the time frame of the graph instead
    };

    std::uint16_t pageMenuCount() const;
    bool pageMenuEntry(std::uint16_t index, std::uint16_t& section, std::uint16_t& page,
                       std::uint16_t& pages) const;
    void handlePageMenu(InputAction action);
    bool menuEntryName(std::uint16_t index, char* name, std::size_t size, char* detail, std::size_t detailSize) const;
    bool plotUsesSource(std::uint16_t sourceIndex) const;
    void graphValues(const HistoryPlotView& plot, char* output, std::size_t capacity) const;
    void renderPageMenu();
    void renderBars(const char* title, const char* const (&labels)[4], bool upArrow, bool downArrow);
    void renderSleepScreen();
    void renderModal(const ModalState& modal);
    void renderSelection();
    void renderAlert();
    void finishActiveUpdate();
    void drawCentered(std::uint16_t x, std::uint16_t width, std::uint16_t y,
                      const char* text, bool black, std::uint8_t scale);

    slstm32::Runtime runtime_;
    slstm32::epd::AsyncPanel& panel_;
    slstm32::epd::MonochromeCanvas& canvas_;
    DataStore& data_;
    HistoryStore& history_;
    LocalClock clock_;
    DisplayPolicy policy_;
    ModalNotification notification_;
    NavigationNotification navigationNotification_;
    ActionHandler actionHandler_;
    HeaderStatus headerStatus_;
    HistoryHandler historyHandler_{};
    std::uint16_t startPage_{noIndex};
    bool startGraph_{};
    PackageView package_{};
    ModalState modal_{};
    ModalState transientModal_{};
    SelectionState selection_{};
    AlertState alert_{};
    PageMenuState pageMenu_{};
    std::uint16_t activeSection_{noIndex};
    std::uint16_t activePage_{noIndex};
    std::uint32_t renderAt_{};
    std::uint32_t lastRenderStartedAt_{};
    std::uint32_t recoverAt_{};
    std::uint32_t partialRefreshCount_{};
    std::uint32_t clockRefreshAt_{};
    std::uint32_t ageCheckedAt_{};
    bool configured_{};
    bool graphMode_{};
    bool dirty_{};
    bool activeUpdate_{};
    bool activeFullRefresh_{};
    bool forceFullRefresh_{};
    bool sleepRequested_{};
    bool shutdownRequested_{};
    bool sleeping_{};
    bool initialized_{};
    bool renderingPaused_{};
    bool modalPresentationPending_{};
    bool activeModalPresentation_{};
    bool alertPresentationPending_{};
    bool activeAlertPresentation_{};
};

} // namespace semantic_display
