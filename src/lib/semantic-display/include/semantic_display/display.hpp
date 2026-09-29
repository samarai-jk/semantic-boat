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
    void showAlert(const char* title, const char* message, AlertLevel level,
                   std::uint8_t position, std::uint8_t count,
                   std::uint16_t overflowCount, AlertHandler handler);
    void updateAlertQueue(std::uint8_t count, std::uint16_t overflowCount);
    void dismissAlert();
    void setRenderingPaused(bool paused);
    void setHeaderStatus(HeaderStatus status) { headerStatus_ = status; }

    std::uint16_t activeSection() const { return activeSection_; }
    std::uint16_t activePage() const { return activePage_; }
    bool modalVisible() const {
        return modal_.visible || transientModal_.visible || selection_.visible;
    }
    bool selectionVisible() const { return selection_.visible; }
    bool alertVisible() const { return alert_.visible; }
    bool sleeping() const { return sleeping_; }
    bool sleepActive() const { return sleeping_ || sleepRequested_; }
    std::uint32_t partialRefreshCount() const { return partialRefreshCount_; }

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
    void renderWidget(const PageView& page, const WidgetView& widget,
                      std::uint16_t x, std::uint16_t y,
                      std::uint16_t width, std::uint16_t height);
    void renderWidgetSeparators(const PageView& page, std::uint16_t contentX,
                                std::uint16_t contentY, std::uint16_t cellWidth,
                                std::uint16_t cellHeight, std::uint16_t gap);
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
    PackageView package_{};
    ModalState modal_{};
    ModalState transientModal_{};
    SelectionState selection_{};
    AlertState alert_{};
    std::uint16_t activeSection_{noIndex};
    std::uint16_t activePage_{noIndex};
    std::uint32_t renderAt_{};
    std::uint32_t lastRenderStartedAt_{};
    std::uint32_t recoverAt_{};
    std::uint32_t partialRefreshCount_{};
    std::uint32_t clockRefreshAt_{};
    bool configured_{};
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
