// Host tests of the gauge widget and named states: compile, parse, render.
// Writes the rendered screens as PGM images when given a directory:
//   semantic_display_gauge_tests build/gauge
#include "semantic_display/compiler.hpp"
#include "semantic_display/data.hpp"
#include "semantic_display/display.hpp"
#include "semantic_display/history.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

std::uint32_t now = 1000u;
std::uint32_t millis() { return now; }

class FakePanel final : public slstm32::epd::AsyncPanel {
public:
    bool init() override { return true; }
    void run() override {}
    std::uint16_t width() const override { return 280u; }
    std::uint16_t height() const override { return 480u; }
    std::size_t frameSize() const override { return 16800u; }
    bool display(const std::uint8_t*, std::size_t size, slstm32::epd::RefreshMode) override { return size == frameSize(); }
    void sleep() override {}
    bool beginDisplay(const std::uint8_t*, std::size_t, slstm32::epd::RefreshMode, slstm32::epd::Region) override {
        state = slstm32::epd::UpdateState::prepared;
        return true;
    }
    bool commitDisplay() override { state = slstm32::epd::UpdateState::refreshing; return true; }
    bool cancelDisplay() override { state = slstm32::epd::UpdateState::idle; return true; }
    slstm32::epd::UpdateState updateState() const override { return state; }
    slstm32::epd::UpdateState state{slstm32::epd::UpdateState::idle};
};

bool black(const std::array<std::uint8_t, 16800>& frame, std::uint16_t x, std::uint16_t y) {
    const auto rawX = y;
    const auto rawY = static_cast<std::uint16_t>(480u - x - 1u);
    return (frame[static_cast<std::size_t>(rawY) * 35u + rawX / 8u] & (0x80u >> (rawX & 7u))) == 0u;
}

void save(const std::array<std::uint8_t, 16800>& frame, const char* directory, const char* name) {
    if (!directory) return;
    const auto path = std::string(directory) + "/" + name + ".pgm";
    auto* file = std::fopen(path.c_str(), "wb");
    if (!file) return;
    std::fprintf(file, "P5 480 280 255\n");
    for (std::uint16_t y = 0u; y < 280u; ++y)
        for (std::uint16_t x = 0u; x < 480u; ++x) std::fputc(black(frame, x, y) ? 0 : 255, file);
    std::fclose(file);
}

constexpr auto json = R"json({
  "schema":"semantic-display/v1",
  "sections":[{"id":"engine","title":"ENGINE","sources":[
      {"id":"rpm","path":"propulsion.engine.revolutions","unit":"Hz"},
      {"id":"hours","path":"propulsion.engine.runTime","unit":"s"},
      {"id":"ign","path":"engine.ignition.state","unit":""},
      {"id":"dplus","path":"electrical.engine.alternator.D.voltage","unit":"V"},
      {"id":"fwd","path":"engine.gearbox.forward.state","unit":""},
      {"id":"coolant","path":"electrical.engine.coolant.sender.voltage","unit":"V"},
      {"id":"battery","path":"electrical.engine.system.voltage","unit":"V"},
      {"id":"exhaust","path":"environment.engine.exhaust.dry.temperature","unit":"K"}],
    "pages":[{"id":"dial","grid":{"columns":1,"rows":1},"widgets":[
      {"type":"gauge","source":"rpm","display_unit":"rpm","label":"RPM","cell":{"column":0,"row":0},
       "min":0,"max":3000,"major":500,"minor":100,"label_step":500,"label_divisor":100,"redline":2700,
       "values":[{"source":"hours","display_unit":"h","decimals":1,"text":"H"}],
       "chips":[{"source":"ign","text":"IGN"},{"source":"dplus","text":"CHG","above":13},{"source":"fwd","text":"FWD"}],
       "side":[{"source":"coolant","text":"COOLANT SENDER","display_unit":"V","decimals":2},
               {"source":"battery","text":"BATTERY","display_unit":"V","decimals":2},
               {"source":"exhaust","text":"DRY EXHAUST","display_unit":"C","decimals":0}]}]},
     {"id":"inputs","grid":{"columns":2,"rows":1},"widgets":[
      {"type":"value","source":"ign","label":"IGNITION","states":["OFF","ON"],"max_digits":3,"cell":{"column":0,"row":0}},
      {"type":"value","source":"fwd","label":"FORWARD","states":["OFF","ON"],"max_digits":3,"cell":{"column":1,"row":0}}]}]}]
})json";

} // namespace

int main(int argc, char** argv) {
    const char* directory = argc > 1 ? argv[1] : nullptr;
    std::array<std::uint8_t, 4096> packageBytes{};
    const auto compiled = semantic_display::ConfigCompiler{}.compile(json, packageBytes.data(), packageBytes.size());
    if (!compiled) std::printf("compile error: %s\n", compiled.message);
    assert(compiled);
    semantic_display::PackageView package{packageBytes.data(), compiled.packageSize};

    // The gauge record: range, ticks, labels, and its further values resolved.
    semantic_display::RecordView record{};
    semantic_display::GaugeView gauge{};
    bool found{};
    assert(package.first(record));
    do {
        semantic_display::WidgetView widget{};
        if (package.widget(record, widget) && widget.type == semantic_display::WidgetType::gauge) {
            found = package.gauge(widget, gauge);
        }
    } while (package.next(record));
    assert(found);
    assert(gauge.minimum == 0.0f && gauge.maximum == 3000.0f && gauge.redline == 2700.0f);
    assert(gauge.major == 500.0f && gauge.minor == 100.0f && gauge.labelDivisor == 100.0f);
    assert(gauge.referenceCount == 7u);
    assert(gauge.references[0].kind == semantic_display::GaugeReferenceKind::value && gauge.references[0].sourceIndex == 1u);
    assert(gauge.references[2].kind == semantic_display::GaugeReferenceKind::chip && gauge.references[2].threshold == 13.0f);
    assert(gauge.references[6].kind == semantic_display::GaugeReferenceKind::side && gauge.references[6].text == "DRY EXHAUST");

    // A gauge without a range is refused.
    std::array<std::uint8_t, 2048> other{};
    assert(!semantic_display::ConfigCompiler{}.compile(R"({"schema":"semantic-display/v1","sections":[{"id":"s","sources":[{"id":"a","path":"a"}],
        "pages":[{"id":"p","grid":{"columns":1,"rows":1},"widgets":[{"type":"gauge","source":"a","cell":{"column":0,"row":0}}]}]}]})",
        other.data(), other.size()));

    std::array<semantic_display::ValueSlot, 8> slots{};
    std::array<std::uint8_t, 256> historyMemory{};
    semantic_display::DataStore data{slots.data(), slots.size()};
    semantic_display::HistoryStore history{historyMemory.data(), historyMemory.size()};
    std::array<std::uint8_t, 16800> frame{};
    slstm32::epd::MonochromeCanvas canvas{frame.data(), frame.size(), 280u, 480u, slstm32::epd::Rotation::degrees90};
    FakePanel panel;
    semantic_display::DisplayService display{{&millis, nullptr, nullptr}, panel, canvas, data, history};
    assert(display.setPackage(package));
    assert(display.init());
    save(frame, directory, "gauge-empty");

    const float values[]{20.0f, 607.1f * 3600.0f, 1.0f, 14.6f, 0.0f, 7.47f, 14.55f, 348.15f};
    for (std::uint16_t index = 0u; index < 8u; ++index) {
        assert(data.setNumber(index, values[index], now));
        display.sourceUpdated(index, true);
    }
    now += 5000u;
    display.run();
    panel.state = slstm32::epd::UpdateState::idle;
    save(frame, directory, "gauge-1200");
    // The needle at 1200 rpm points up and to the left of the top (1500 rpm).
    // The hub is drawn; the IGN chip is lit (black), the FWD chip is not.
    bool needle{};
    for (std::uint16_t y = 60u; y < 120u; ++y)
        for (std::uint16_t x = 70u; x < 140u; ++x) needle = needle || black(frame, x, y);
    assert(needle);

    display.handle(semantic_display::InputAction::nextPage);
    for (int step = 0; step < 10; ++step) {
        now += 1000u;
        display.run();
        panel.state = slstm32::epd::UpdateState::idle;
    }
    save(frame, directory, "states");
    // IGNITION ON: the left half has more black than the right (FORWARD OFF
    // has one letter more but thinner); both values are drawn.
    std::size_t left{}, right{};
    for (std::uint16_t y = 60u; y < 220u; ++y)
        for (std::uint16_t x = 10u; x < 470u; ++x) (x < 240u ? left : right) += black(frame, x, y) ? 1u : 0u;
    assert(left > 500u && right > 500u);
    std::puts("gauge tests passed");
    return 0;
}
