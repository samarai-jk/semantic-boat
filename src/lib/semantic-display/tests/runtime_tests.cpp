#include "semantic_display/compiler.hpp"
#include "semantic_display/data.hpp"
#include "semantic_display/display.hpp"
#include "semantic_display/history.hpp"
#include "slstm32/epd/font5x7.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>

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
    assert(semantic_display::formatPosition("50.80000N 1.30000W", semantic_display::PositionFormat::ddm, formatted, sizeof formatted));
    assert(std::strcmp(formatted, "50\xc2\xb0" "48.000'N 1\xc2\xb0" "18.000'W") == 0);
    assert(semantic_display::formatPosition("50.80000N 1.30000W", semantic_display::PositionFormat::dd, formatted, sizeof formatted));
    assert(std::strcmp(formatted, "50.800000, -1.300000") == 0);
    assert(semantic_display::formatPosition("50.80000N 1.30000W", semantic_display::PositionFormat::dms, formatted, sizeof formatted));
    assert(std::strcmp(formatted, "50\xc2\xb0" "48'0.0\"N 1\xc2\xb0" "18'0.0\"W") == 0);
    assert(semantic_display::formatPosition("89.99999999S 179.99999999E", semantic_display::PositionFormat::ddm, formatted, sizeof formatted));
    assert(std::strcmp(formatted, "90\xc2\xb0" "0.000'S 180\xc2\xb0" "0.000'E") == 0);
    assert(semantic_display::formatPosition("89.99999999S 179.99999999E", semantic_display::PositionFormat::dms, formatted, sizeof formatted));
    assert(std::strcmp(formatted, "90\xc2\xb0" "0'0.0\"S 180\xc2\xb0" "0'0.0\"E") == 0);
    assert(semantic_display::formatPosition("48.858370N 2.294481E", semantic_display::PositionFormat::dd, formatted, sizeof formatted));
    assert(std::strcmp(formatted, "48.858370, 2.294481") == 0);
    assert(semantic_display::formatPosition("48.858370N 2.294481E", semantic_display::PositionFormat::dms, formatted, sizeof formatted));
    assert(std::strcmp(formatted, "48\xc2\xb0" "51'30.1\"N 2\xc2\xb0" "17'40.1\"E") == 0);
    assert(semantic_display::formatPosition("48.858370N 2.294481E", semantic_display::PositionFormat::ddm, formatted, sizeof formatted));
    assert(std::strcmp(formatted, "48\xc2\xb0" "51.502'N 2\xc2\xb0" "17.669'E") == 0);
    assert(semantic_display::formatPosition("1.000001S 2.000001W", semantic_display::PositionFormat::dd, formatted, sizeof formatted));
    assert(std::strcmp(formatted, "-1.000001, -2.000001") == 0);
    assert(slstm32::epd::Font5x7::textWidth("1\xc2\xb0" "2'3\"", 1u) == 35u);
    assert(slstm32::epd::Font5x7::textWidthScaled("1\xc2\xb0" "2'3\"", 3u, 2u) == 53u);
    {
      std::array<std::uint8_t, 7> glyphFrame{}; glyphFrame.fill(0xffu);
      slstm32::epd::MonochromeCanvas glyphCanvas{glyphFrame.data(), glyphFrame.size(), 8u, 7u};
      for (const auto symbol : {"\xc2\xb0", "'", "\"", ","}) {
        glyphFrame.fill(0xffu);
        slstm32::epd::Font5x7::drawText(glyphCanvas, 0u, 0u, symbol, true, 1u);
            assert((glyphFrame != std::array<std::uint8_t, 7>{0xffu,0xffu,0xffu,0xffu,0xffu,0xffu,0xffu}));
      }
    }
    for (const auto invalidPosition : {"91.00000N 1.00000E", "1.00000N 181.00000W", "nanN 1E", "1N", "1E 2N", "1N 2E trailing"}) {
      assert(!semantic_display::formatPosition(invalidPosition, semantic_display::PositionFormat::ddm, formatted, sizeof formatted));
    }
    assert(!semantic_display::formatPosition("0N 0E", semantic_display::PositionFormat::ddm, formatted, 4u));
    semantic_display::SourceView ageSource{};
    ageSource.outdatedAfterMs = 10000u; ageSource.staleAfterMs = 7200000u;
    semantic_display::ValueSlot ageValue{};
    ageValue.type = semantic_display::ValueType::number; ageValue.updatedAt = 100u;
    assert(semantic_display::valueFreshness(ageSource, &ageValue, 10099u) == semantic_display::ValueFreshness::fresh);
    assert(semantic_display::valueFreshness(ageSource, &ageValue, 10100u) == semantic_display::ValueFreshness::outdated);
    assert(semantic_display::valueFreshness(ageSource, &ageValue, 7200100u) == semantic_display::ValueFreshness::stale);
    for (const auto age : {10000u, 60000u, 3600000u}) {
      assert(semantic_display::formatValueAge(age, formatted, sizeof formatted));
      assert(std::strcmp(formatted, age == 10000u ? "OLD 10s" : age == 60000u ? "OLD 1m" : "OLD 1h") == 0);
    }
    ageValue.updatedAt = 0xfffffff0u;
    assert(semantic_display::valueFreshness(ageSource, &ageValue, 0x2700u) == semantic_display::ValueFreshness::outdated);
    ageValue.type = semantic_display::ValueType::unavailable;
    assert(semantic_display::valueFreshness(ageSource, &ageValue, 0u) == semantic_display::ValueFreshness::unavailable);

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

    {
      constexpr auto graphJson = R"json({"schema":"semantic-display/v1",
        "permanent_sources":[{"id":"series","path":"test.series","unit":"K",
          "history":{"interval_ms":100,"points":3,"reducer":"last"}}],
        "sections":[{"id":"graphs","pages":[
          {"id":"plot","grid":{"columns":1,"rows":1},"widgets":[
            {"type":"text","text":"NORMAL","cell":{"column":0,"row":0}}],
            "history_plot":{"source":"series","title":"TEST GRAPH","display_unit":"C","y_min":-10,"y_max":10}},
          {"id":"plain","grid":{"columns":1,"rows":1},"widgets":[]},
          {"id":"auto","grid":{"columns":1,"rows":1},"widgets":[],
            "history_plot":{"source":"series","display_unit":"C"}}]}]})json";
      std::array<std::uint8_t, 1024> graphBytes{};
      const auto graphPackage = semantic_display::ConfigCompiler{}.compile(graphJson, graphBytes.data(), graphBytes.size());
      assert(graphPackage);
      std::array<semantic_display::ValueSlot, 1> graphSlots{};
      semantic_display::DataStore graphData{graphSlots.data(), graphSlots.size()};
      std::array<std::uint8_t, 64> graphHistoryBytes{};
      semantic_display::HistoryStore graphHistory{graphHistoryBytes.data(), graphHistoryBytes.size()};
      std::array<std::uint8_t, 16800> graphFrame{};
      slstm32::epd::MonochromeCanvas graphCanvas{graphFrame.data(), graphFrame.size(), 280u, 480u, slstm32::epd::Rotation::degrees90};
      FakePanel graphPanel;
      semantic_display::DisplayService graphDisplay{{&millis, nullptr, nullptr}, graphPanel, graphCanvas, graphData, graphHistory};
      now = 0u;
      assert(graphDisplay.setPackage({graphBytes.data(), graphPackage.packageSize}));
      assert(graphDisplay.init());
      const auto normalFrame = graphFrame;
      graphDisplay.handle(semantic_display::InputAction::action1);
      assert(graphDisplay.graphMode());
      now = 100u; graphDisplay.run();
      assert(graphFrame != normalFrame);
      const auto emptyGraph = graphFrame;
      for (std::uint16_t row = 70u; row < 245u; ++row) {
        assert(!logicalPixelIsBlack(graphFrame, 6u, row));
        assert(!logicalPixelIsBlack(graphFrame, 7u, row));
      }
      for (std::uint16_t column = 6u; column < 474u; ++column) {
        assert(!logicalPixelIsBlack(graphFrame, column, 245u));
        assert(!logicalPixelIsBlack(graphFrame, column, 244u));
      }
      for (std::uint16_t row = 246u; row < 252u; ++row)
        for (std::uint16_t column = 6u; column < 474u; ++column)
          assert(!logicalPixelIsBlack(graphFrame, column, row));
      for (std::uint16_t row = 30u; row < 36u; ++row)
        for (std::uint16_t column = 6u; column < 474u; ++column)
          assert(!logicalPixelIsBlack(graphFrame, column, row));
      graphDisplay.run(); graphPanel.state = slstm32::epd::UpdateState::idle; graphDisplay.run();
      for (const auto value : {275.15f, 278.15f, 281.15f}) {
        now += 100u; assert(graphData.setNumber(0u, value, now)); graphDisplay.sourceUpdated(0u);
      }
      now += 333u; graphDisplay.run();
      assert(graphFrame != emptyGraph);
      assert(graphHistory.sampleCount(0u) == 3u);
      std::array<std::uint8_t, 16800> upperLabelFrame{};
      slstm32::epd::MonochromeCanvas upperLabelCanvas{upperLabelFrame.data(), upperLabelFrame.size(), 280u, 480u, slstm32::epd::Rotation::degrees90};
      upperLabelCanvas.clear(false);
      slstm32::epd::Font5x7::drawTextScaled(upperLabelCanvas, 6u, 57u, "10.0 C", true, 3u, 2u);
      for (std::uint16_t row = 57u; row < 68u; ++row)
        for (std::uint16_t column = 6u; column < 59u; ++column)
          assert(logicalPixelIsBlack(graphFrame, column, row) == logicalPixelIsBlack(upperLabelFrame, column, row));
      for (std::uint16_t row = 41u; row < 52u; ++row)
        for (std::uint16_t column = 6u; column < 59u; ++column)
          assert(!logicalPixelIsBlack(graphFrame, column, row));
      assert(logicalPixelIsBlack(graphFrame, 473u, 76u));
      assert(logicalPixelIsBlack(graphFrame, 473u, 77u));
      assert(logicalPixelIsBlack(graphFrame, 472u, 76u));
      graphDisplay.run(); graphPanel.state = slstm32::epd::UpdateState::idle; graphDisplay.run();
      for (const auto value : {173.15f, 373.15f}) {
        now += 100u; assert(graphData.setNumber(0u, value, now)); graphDisplay.sourceUpdated(0u);
      }
      float oldest{};
      assert(graphHistory.sample(0u, 0u, oldest) && oldest == 281.15f);
      now += 333u; graphDisplay.run();
      assert(logicalPixelIsBlack(graphFrame, 473u, 57u));
      assert(logicalPixelIsBlack(graphFrame, 473u, 58u));
      for (std::uint16_t row = 34u; row < 39u; ++row)
        assert(!logicalPixelIsBlack(graphFrame, 473u, row));
      graphDisplay.run(); graphPanel.state = slstm32::epd::UpdateState::idle; graphDisplay.run();
      graphDisplay.handle(semantic_display::InputAction::action1);
      assert(!graphDisplay.graphMode());
      graphDisplay.handle(semantic_display::InputAction::nextPage);
      graphDisplay.handle(semantic_display::InputAction::nextPage);
      graphDisplay.handle(semantic_display::InputAction::action1);
      assert(graphDisplay.graphMode());
      for (unsigned sample = 0u; sample < 3u; ++sample) {
        now += 100u; assert(graphData.setNumber(0u, 278.15f, now)); graphDisplay.sourceUpdated(0u);
      }
      now += 333u; graphDisplay.run();
      assert(logicalPixelIsBlack(graphFrame, 473u, 57u));
      assert(!logicalPixelIsBlack(graphFrame, 473u, 145u));
      graphDisplay.run(); graphPanel.state = slstm32::epd::UpdateState::idle; graphDisplay.run();
      for (const auto value : {275.15f, 278.15f, 281.15f}) {
        now += 100u; assert(graphData.setNumber(0u, value, now)); graphDisplay.sourceUpdated(0u);
      }
      now += 333u; graphDisplay.run();
      assert(logicalPixelIsBlack(graphFrame, 239u, 128u));
      assert(logicalPixelIsBlack(graphFrame, 239u, 129u));
      assert(!logicalPixelIsBlack(graphFrame, 239u, 140u));
      graphDisplay.run(); graphPanel.state = slstm32::epd::UpdateState::idle; graphDisplay.run();
      for (const auto value : {273.15f, 268.15f}) {
        for (unsigned sample = 0u; sample < 3u; ++sample) {
          now += 100u; assert(graphData.setNumber(0u, value, now)); graphDisplay.sourceUpdated(0u);
        }
        now += 333u; graphDisplay.run();
        assert(logicalPixelIsBlack(graphFrame, 473u, 245u));
        assert(!logicalPixelIsBlack(graphFrame, 473u, 243u));
        graphDisplay.run(); graphPanel.state = slstm32::epd::UpdateState::idle; graphDisplay.run();
      }
      graphDisplay.handle(semantic_display::InputAction::nextPage);
      now += 100u; graphDisplay.run();
      assert(graphFrame == normalFrame);
      graphDisplay.run(); graphPanel.state = slstm32::epd::UpdateState::idle; graphDisplay.run();
      graphDisplay.handle(semantic_display::InputAction::action1);
      graphDisplay.handle(semantic_display::InputAction::nextPage);
      assert(!graphDisplay.graphMode());
      graphDisplay.handle(semantic_display::InputAction::action1);
      assert(!graphDisplay.graphMode());
      for (const auto interval : {900000u, 18000000u}) {
        std::string durationJson{graphJson};
        constexpr auto intervalProperty = "\"interval_ms\":100";
        durationJson.replace(durationJson.find(intervalProperty), std::strlen(intervalProperty),
                             "\"interval_ms\":" + std::to_string(interval));
        const auto durationPackage = semantic_display::ConfigCompiler{}.compile(durationJson, graphBytes.data(), graphBytes.size());
        assert(durationPackage);
        FakePanel durationPanel;
        semantic_display::DisplayService durationDisplay{{&millis, nullptr, nullptr}, durationPanel, graphCanvas, graphData, graphHistory};
        now = 0u;
        assert(durationDisplay.setPackage({graphBytes.data(), durationPackage.packageSize}));
        assert(durationDisplay.init());
        durationDisplay.handle(semantic_display::InputAction::action1);
        now = 100u; durationDisplay.run();
        const auto label = interval == 900000u ? "30m" : "10h";
        const auto labelX = static_cast<std::uint16_t>(474u - slstm32::epd::Font5x7::textWidthScaled(label, 3u, 2u));
        std::array<std::uint8_t, 16800> expectedFrame{};
        slstm32::epd::MonochromeCanvas expectedCanvas{expectedFrame.data(), expectedFrame.size(), 280u, 480u, slstm32::epd::Rotation::degrees90};
        expectedCanvas.clear(false);
        slstm32::epd::Font5x7::drawTextScaled(expectedCanvas, labelX, 41u, label, true, 3u, 2u);
        for (std::uint16_t row = 41u; row < 52u; ++row)
          for (auto column = static_cast<std::uint16_t>(labelX - 9u); column < 474u; ++column)
            assert(logicalPixelIsBlack(graphFrame, column, row) == logicalPixelIsBlack(expectedFrame, column, row));
        constexpr auto graphTitle = "TEST GRAPH";
        const auto titleWidth = slstm32::epd::Font5x7::textWidth(graphTitle, 2u);
        const auto titleX = static_cast<std::uint16_t>((480u - titleWidth) / 2u);
        slstm32::epd::Font5x7::drawText(expectedCanvas, titleX, 36u, graphTitle, true, 2u);
        for (std::uint16_t row = 36u; row < 50u; ++row)
          for (auto column = titleX; column < titleX + titleWidth; ++column)
            assert(logicalPixelIsBlack(graphFrame, column, row) == logicalPixelIsBlack(expectedFrame, column, row));
      }
    }
    {
      constexpr auto agingJson = R"json({"schema":"semantic-display/v1","sections":[{"id":"age",
        "sources":[{"id":"position","path":"navigation.position","outdated_ms":10000,"stale_ms":7200000}],
        "pages":[{"id":"position","grid":{"columns":1,"rows":1},"widgets":[
        {"type":"value","source":"position","cell":{"column":0,"row":0}}]}]}]})json";
      std::array<std::uint8_t, 1024> agingBytes{};
      const auto agingPackage = semantic_display::ConfigCompiler{}.compile(agingJson, agingBytes.data(), agingBytes.size());
      assert(agingPackage);
      std::array<semantic_display::ValueSlot, 1> agingSlots{};
      semantic_display::DataStore agingData{agingSlots.data(), agingSlots.size()};
      std::array<std::uint8_t, 64> agingHistoryBytes{};
      semantic_display::HistoryStore agingHistory{agingHistoryBytes.data(), agingHistoryBytes.size()};
      std::array<std::uint8_t, 16800> agingFrame{};
      slstm32::epd::MonochromeCanvas agingCanvas{agingFrame.data(), agingFrame.size(), 280u, 480u, slstm32::epd::Rotation::degrees90};
      FakePanel agingPanel;
      semantic_display::DisplayService agingDisplay{{&millis, nullptr, nullptr}, agingPanel, agingCanvas, agingData, agingHistory};
      assert(agingDisplay.setPackage({agingBytes.data(), agingPackage.packageSize}));
      now += 1000u;
      assert(agingDisplay.init());
      const auto receivedAt = now;
      assert(agingData.setText(0u, "60.25000N 5.75000W", now)); agingDisplay.sourceUpdated(0u);
      const auto finishRefresh = [&]() {
        assert(agingPanel.state == slstm32::epd::UpdateState::prepared);
        agingDisplay.run(); agingPanel.state = slstm32::epd::UpdateState::idle; agingDisplay.run();
      };
      now += 333u; agingDisplay.run(); finishRefresh();
      const auto freshFrame = agingFrame;
      now = receivedAt + 10000u; agingDisplay.run(); finishRefresh();
      assert(agingFrame != freshFrame);
      const auto outdatedFrame = agingFrame;
      now = receivedAt + 60000u; agingDisplay.run(); finishRefresh();
      assert(agingFrame != outdatedFrame);
      const auto minuteFrame = agingFrame;
      now = receivedAt + 3600000u; agingDisplay.run(); finishRefresh();
      assert(agingFrame != minuteFrame);
      const auto hourFrame = agingFrame;
      now = receivedAt + 7200000u; agingDisplay.run(); finishRefresh();
      assert(agingFrame != hourFrame);
      now += 1000u;
      assert(agingData.setText(0u, "60.25000N 5.75000W", now)); agingDisplay.sourceUpdated(0u);
      now += 333u; agingDisplay.run(); finishRefresh();
      assert(agingFrame == freshFrame);
    }
    display.requestShutdown();
    assert(!display.wake());
    display.run();
    assert(display.sleeping());
    assert(panel.sleepCalls == 2u);
}
