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
      {"id":"a","grid":{"columns":1,"rows":1},"widgets":[
        {"type":"value","source":"history","cell":{"column":0,"row":0}}
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
                                              data, history, {}, {}, {}, {},
                                              {nullptr, &actionAvailable, &executeAction,
                                               &actionLabel}};
    assert(display.setPackage(package));
    assert(display.init());
    assert(display.activeSection() == 0u && display.activePage() == 0u);

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
    assert(actionLabelCalls != 0u);
    display.run();
    assert(panel.state == slstm32::epd::UpdateState::refreshing);
    panel.state = slstm32::epd::UpdateState::idle;
    display.run();
    display.handle(semantic_display::InputAction::previousSection);
    assert(display.activeSection() == 0u && display.activePage() == 0u);

    display.showModal("ERROR", "TEST", semantic_display::ModalSeverity::error);
    assert(display.modalVisible());
    display.handle(semantic_display::InputAction::action1);
    assert(!display.modalVisible());

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
}
