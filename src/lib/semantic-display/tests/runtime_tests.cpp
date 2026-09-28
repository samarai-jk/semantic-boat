#include "semantic_display/compiler.hpp"
#include "semantic_display/data.hpp"
#include "semantic_display/display.hpp"
#include "semantic_display/history.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>

namespace {

std::uint32_t now;
std::uint32_t millis() { return now; }
bool allowAction{};
unsigned actionCalls{};
unsigned actionLabelCalls{};
semantic_display::SelectionResult selectionResult{};
std::size_t selectionIndex{};
unsigned selectionCalls{};
unsigned modalChangedCalls{};
unsigned modalPresentedCalls{};
bool modalVisible{};
semantic_display::ModalSeverity presentedSeverity{};
unsigned alertPresentedCalls{};
unsigned alertActionCalls{};
semantic_display::AlertAction selectedAlertAction{};

void modalChanged(void*, bool visible, semantic_display::ModalSeverity) {
    modalVisible = visible;
    ++modalChangedCalls;
}

void modalPresented(void*, semantic_display::ModalSeverity severity) {
    presentedSeverity = severity;
    ++modalPresentedCalls;
}

void alertPresented(void*) {
    ++alertPresentedCalls;
}

void alertSelected(void*, semantic_display::AlertAction action) {
    selectedAlertAction = action;
    ++alertActionCalls;
}

std::string_view selectionItem(void*, std::size_t index) {
    constexpr std::string_view items[]{"ONE", "TWO", "THREE"};
    return index < 3u ? items[index] : std::string_view{};
}

void selectionCompleted(void*, semantic_display::SelectionResult result,
                        std::size_t index) {
    selectionResult = result;
    selectionIndex = index;
    ++selectionCalls;
}

bool actionAvailable(void*, semantic_display::InputAction action, std::uint16_t,
                     std::uint16_t) {
    return allowAction && action == semantic_display::InputAction::action1;
}

bool executeAction(void*, semantic_display::InputAction action, std::uint16_t,
                   std::uint16_t) {
    if (action != semantic_display::InputAction::action1) return false;
    ++actionCalls;
    return true;
}

std::string_view actionLabel(void*, semantic_display::InputAction action, std::uint16_t,
                             std::uint16_t) {
    if (action != semantic_display::InputAction::action1) return {};
    ++actionLabelCalls;
    return "TEST";
}

bool logicalPixelIsBlack(const std::array<std::uint8_t, 16800>& frame,
                         std::uint16_t x, std::uint16_t y) {
    // The runtime test canvas is a 280x480 raw buffer rotated by 90 degrees.
    const auto rawX = y;
    const auto rawY = static_cast<std::uint16_t>(480u - x - 1u);
    const auto index = static_cast<std::size_t>(rawY) * 35u + rawX / 8u;
    const auto mask = static_cast<std::uint8_t>(0x80u >> (rawX & 7u));
    return (frame[index] & mask) == 0u;
}

class FakePanel final : public slstm32::epd::AsyncPanel {
public:
    bool init() override {
        ++initCalls;
        state = slstm32::epd::UpdateState::idle;
        return true;
    }
    void run() override {}
    std::uint16_t width() const override { return 280u; }
    std::uint16_t height() const override { return 480u; }
    std::size_t frameSize() const override { return 16800u; }
    bool display(const std::uint8_t*, std::size_t size, slstm32::epd::RefreshMode) override {
        return size == frameSize();
    }
    void sleep() override { ++sleepCalls; }
    bool beginDisplay(const std::uint8_t*, std::size_t size, slstm32::epd::RefreshMode mode,
                      slstm32::epd::Region) override {
        if (size != frameSize() || state != slstm32::epd::UpdateState::idle) return false;
        lastMode = mode;
        state = slstm32::epd::UpdateState::prepared;
        return true;
    }
    bool commitDisplay() override {
        if (state != slstm32::epd::UpdateState::prepared) return false;
        state = slstm32::epd::UpdateState::refreshing;
        return true;
    }
    bool cancelDisplay() override {
        if (!canCancelDisplay()) return false;
        state = slstm32::epd::UpdateState::idle;
        return true;
    }
    slstm32::epd::UpdateState updateState() const override { return state; }
    slstm32::epd::UpdateState state{slstm32::epd::UpdateState::idle};
    slstm32::epd::RefreshMode lastMode{slstm32::epd::RefreshMode::partial};
    unsigned initCalls{};
    unsigned sleepCalls{};
};

constexpr auto json = R"json({
  "schema":"semantic-display/v1",
  "permanent_sources":[
    {"id":"history","provider":"fake","path":"history","unit":"m",
     "history":{"interval_ms":100,"points":2,"reducer":"mean"}}
  ],
  "sections":[
    {"id":"one","pages":[
      {"id":"a","grid":{"columns":2,"rows":1},"widgets":[
        {"type":"value","source":"history","cell":{"column":0,"row":0}},
        {"type":"value","source":"history","cell":{"column":1,"row":0}}
      ]},
      {"id":"b","grid":{"columns":1,"rows":1},"widgets":[
        {"type":"not-installed","cell":{"column":0,"row":0}}
      ]}
    ]},
    {"id":"two","sources":[
      {"id":"section-two","provider":"fake","path":"section-two"}
    ],"pages":[
      {"id":"c","grid":{"columns":1,"rows":1},"widgets":[
        {"type":"local-clock","cell":{"column":0,"row":0}}
      ]}
    ]}
  ]
})json";

} // namespace

int main() {
    assert(std::fabs(semantic_display::convertUnit(1852.0f, "m", "nm") - 1.0f) < 0.001f);
    assert(std::fabs(semantic_display::convertUnit(101320.0f, "Pa", "hPa") - 1013.2f) < 0.01f);
    assert(std::fabs(semantic_display::convertUnit(370000.0f, "Pa", "bar") - 3.7f) < 0.001f);
    assert(std::fabs(semantic_display::convertUnit(30.0f, "Hz", "rpm") - 1800.0f) < 0.01f);
    assert(std::fabs(semantic_display::convertUnit(7200.0f, "s", "h") - 2.0f) < 0.001f);
    assert(std::fabs(semantic_display::convertUnit(0.72f, "ratio", "%") - 72.0f) < 0.001f);
    assert(std::fabs(semantic_display::convertUnit(1.0e-6f, "m3/s", "L/h") - 3.6f) < 0.001f);

    char formatted[32]{};
    assert(semantic_display::formatNumber(12.345f, 2u, formatted, sizeof formatted));
    assert(std::strcmp(formatted, "12.35") == 0);
    assert(semantic_display::formatNumber(-0.05f, 2u, formatted, sizeof formatted));
    assert(std::strcmp(formatted, "-0.05") == 0);
    assert(semantic_display::formatNumber(3.0f, 0u, formatted, sizeof formatted));
    assert(std::strcmp(formatted, "3") == 0);

    std::array<std::uint8_t, 2048> packageBytes{};
    const auto compiled = semantic_display::ConfigCompiler{}.compile(
        json, packageBytes.data(), packageBytes.size());
    assert(compiled);
    semantic_display::PackageView package{packageBytes.data(), compiled.packageSize};

    std::array<semantic_display::ValueSlot, 4> slots{};
    std::array<std::uint8_t, 256> historyMemory{};
    semantic_display::DataStore data{slots.data(), slots.size()};
    semantic_display::HistoryStore history{historyMemory.data(), historyMemory.size()};
    std::array<std::uint8_t, 16800> frame{};
    slstm32::epd::MonochromeCanvas canvas{frame.data(), frame.size(), 280u, 480u,
                                          slstm32::epd::Rotation::degrees90};
    FakePanel panel;
    semantic_display::DisplayService display{{&millis, nullptr, nullptr}, panel, canvas,
                                              data, history, {}, {},
                                              {nullptr, &modalChanged, &modalPresented}, {},
                                              {nullptr, &actionAvailable, &executeAction,
                                               &actionLabel}};
    assert(display.setPackage(package));
    assert(display.init());
    assert(display.activeSection() == 0u && display.activePage() == 0u);
    assert(logicalPixelIsBlack(frame, 464u, 16u));
    assert(!logicalPixelIsBlack(frame, 464u, 262u));
    assert(!logicalPixelIsBlack(frame, 7u, 40u));
    assert(!logicalPixelIsBlack(frame, 237u, 40u));
    assert(logicalPixelIsBlack(frame, 238u, 40u));
    assert(logicalPixelIsBlack(frame, 241u, 40u));
    assert(!logicalPixelIsBlack(frame, 242u, 40u));

    display.handle(semantic_display::InputAction::action1);
    assert(actionCalls == 0u);
    allowAction = true;
    display.handle(semantic_display::InputAction::action1);
    assert(actionCalls == 1u);

    display.handle(semantic_display::InputAction::nextPage);
    assert(display.activePage() == 1u);
    display.handle(semantic_display::InputAction::nextSection);
    assert(display.activeSection() == 1u && display.activePage() == 2u);
    display.handle(semantic_display::InputAction::nextPage);
    assert(display.activePage() == 2u);
    now = 60000u;
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::prepared);
    assert(!logicalPixelIsBlack(frame, 464u, 16u));
    assert(logicalPixelIsBlack(frame, 464u, 262u));
    assert(actionLabelCalls != 0u);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::refreshing);
    panel.state = slstm32::epd::UpdateState::idle;
    display.run();
    display.handle(semantic_display::InputAction::previousSection);
    assert(display.activeSection() == 0u && display.activePage() == 0u);

    display.showModal("ERROR", "TEST", semantic_display::ModalSeverity::error);
    assert(display.modalVisible());
    assert(modalChangedCalls == 1u && modalVisible);
    assert(modalPresentedCalls == 0u);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::prepared);
    // A modal is rendered as an independent screen. The normal page's black
    // status bars must not survive outside the dialog bounds.
    assert(!logicalPixelIsBlack(frame, 464u, 16u));
    assert(!logicalPixelIsBlack(frame, 464u, 262u));
    assert(modalPresentedCalls == 0u);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::refreshing);
    assert(modalPresentedCalls == 0u);
    panel.state = slstm32::epd::UpdateState::idle;
    display.run();
    assert(modalPresentedCalls == 1u);
    assert(presentedSeverity == semantic_display::ModalSeverity::error);
    display.handle(semantic_display::InputAction::action1);
    assert(!display.modalVisible());
    assert(modalChangedCalls == 2u && !modalVisible);

    display.showAlert("ENGINE OVERHEATING", "STOP THE ENGINE",
                      semantic_display::AlertLevel::alarm, 1u, 2u, 0u,
                      {nullptr, &alertSelected, &alertPresented});
    assert(display.alertVisible());
    assert(alertPresentedCalls == 0u);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::prepared);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::refreshing);
    panel.state = slstm32::epd::UpdateState::idle;
    display.run();
    assert(alertPresentedCalls == 1u);
    display.updateAlertQueue(3u, 0u);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::prepared);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::refreshing);
    panel.state = slstm32::epd::UpdateState::idle;
    display.run();
    assert(alertPresentedCalls == 1u);
    display.handle(semantic_display::InputAction::action2);
    assert(alertActionCalls == 1u);
    assert(selectedAlertAction == semantic_display::AlertAction::silence);
    display.dismissAlert();
    assert(!display.alertVisible());

    assert(display.showSelection("SELECT", {nullptr, 3u, &selectionItem,
                                             &selectionCompleted}));
    assert(display.selectionVisible());
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::prepared);
    assert(!logicalPixelIsBlack(frame, 464u, 16u));
    assert(!logicalPixelIsBlack(frame, 464u, 262u));
    display.handle(semantic_display::InputAction::nextSection);
    display.handle(semantic_display::InputAction::nextSection);
    display.handle(semantic_display::InputAction::action2);
    assert(!display.selectionVisible());
    assert(selectionCalls == 1u);
    assert(selectionResult == semantic_display::SelectionResult::selected);
    assert(selectionIndex == 2u);

    assert(display.showSelection("SELECT", {nullptr, 3u, &selectionItem,
                                             &selectionCompleted}, 1u));
    display.handle(semantic_display::InputAction::action1);
    assert(selectionCalls == 2u);
    assert(selectionResult == semantic_display::SelectionResult::cancelled);
    assert(selectionIndex == 1u);

    assert(display.showSelection("SELECT", {nullptr, 3u, &selectionItem,
                                             &selectionCompleted}));
    display.handle(semantic_display::InputAction::previousPage);
    assert(selectionCalls == 3u);
    assert(selectionResult == semantic_display::SelectionResult::cancelled);
    assert(selectionIndex == 0u);

    assert(display.showSelection("SELECT", {nullptr, 3u, &selectionItem,
                                             &selectionCompleted}, 1u));
    display.handle(semantic_display::InputAction::nextPage);
    assert(selectionCalls == 4u);
    assert(selectionResult == semantic_display::SelectionResult::selected);
    assert(selectionIndex == 1u);

    const auto source = data.findSource(package, "fake", "history");
    assert(source != semantic_display::noIndex);
    assert(semantic_display::sourceIsSubscribed(package, source, display.activeSection()));
    const auto sectionTwoSource = data.findSource(package, "fake", "section-two");
    assert(sectionTwoSource != semantic_display::noIndex);
    assert(!semantic_display::sourceIsSubscribed(package, sectionTwoSource,
                                                 display.activeSection()));
    now = 0u; assert(data.setNumber(source, 1.0f, now)); display.sourceUpdated(source);
    now = 50u; assert(data.setNumber(source, 3.0f, now)); display.sourceUpdated(source);
    now = 100u; assert(data.setNumber(source, 5.0f, now)); display.sourceUpdated(source);
    assert(history.sampleCount(source) == 1u);
    float sample{};
    assert(history.sample(source, 0u, sample));
    assert(std::fabs(sample - 3.0f) < 0.001f);

    display.requestFullRefresh();
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::prepared);
    assert(panel.lastMode == slstm32::epd::RefreshMode::full);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::refreshing);
    panel.state = slstm32::epd::UpdateState::idle;
    display.run();

    // A producer may update data/history without making a diagnostic value
    // drive an EPD refresh. The latest value is picked up by the next normal
    // page render.
    now += 100u;
    assert(data.setNumber(source, 9.0f, now));
    display.sourceUpdated(source, false);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::idle);

    // Dialogs render once, then incoming data continues into the data/history
    // stores without scheduling competing EPD work. Dismissal redraws the
    // complete normal page from the latest values.
    display.showModal("PAUSED", "DATA RENDER TEST",
                      semantic_display::ModalSeverity::information);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::prepared);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::refreshing);
    panel.state = slstm32::epd::UpdateState::idle;
    display.run();
    now += 100u;
    assert(data.setNumber(source, 7.0f, now));
    display.sourceUpdated(source);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::idle);
    display.dismissModal();
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::prepared);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::refreshing);
    panel.state = slstm32::epd::UpdateState::idle;
    display.run();

    const auto sectionBeforeSleep = display.activeSection();
    const auto pageBeforeSleep = display.activePage();
    display.showTransientModal("SLEEPING", "", "PRESS ANY BUTTON TO WAKE UP");
    display.requestSleep();
    display.run();
    assert(!display.sleeping());
    assert(panel.state == slstm32::epd::UpdateState::prepared);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::refreshing);
    panel.state = slstm32::epd::UpdateState::idle;
    display.run();
    display.run();
    assert(display.sleeping());
    assert(panel.sleepCalls == 1u);
    display.handle(semantic_display::InputAction::nextPage);
    assert(display.activeSection() == sectionBeforeSleep);
    assert(display.activePage() == pageBeforeSleep);

    assert(display.wake());
    display.dismissTransientModal();
    assert(!display.sleeping());
    assert(panel.initCalls == 1u);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::prepared);
    assert(panel.lastMode == slstm32::epd::RefreshMode::full);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::refreshing);
    panel.state = slstm32::epd::UpdateState::idle;
    display.run();

    constexpr auto singleSectionJson = R"json({
      "schema":"semantic-display/v1",
      "sections":[{"id":"only","pages":[
        {"id":"first","grid":{"columns":1,"rows":1},"widgets":[
          {"type":"text","cell":{"column":0,"row":0},"text":"FIRST"}]},
        {"id":"second","grid":{"columns":1,"rows":1},"widgets":[
          {"type":"text","cell":{"column":0,"row":0},"text":"SECOND"}]}
      ]}]
    })json";
    const auto singleSection = semantic_display::ConfigCompiler{}.compile(
        singleSectionJson, packageBytes.data(), packageBytes.size());
    assert(singleSection);
    assert(display.setPackage({packageBytes.data(), singleSection.packageSize}));
    display.handle(semantic_display::InputAction::nextPage);
    assert(display.activeSection() == 0u && display.activePage() == 1u);
    display.handle(semantic_display::InputAction::previousSection);
    assert(display.activeSection() == 0u && display.activePage() == 1u);
    display.handle(semantic_display::InputAction::nextSection);
    assert(display.activeSection() == 0u && display.activePage() == 1u);
}
