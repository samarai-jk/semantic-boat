#include "semantic_display/compiler.hpp"
#include <array>
#include <cassert>
#include <cstring>

namespace {

constexpr auto configuration = R"json(
{
  "schema": "semantic-display/v1",
  "permanent_sources": [
    {
      "id": "depth-history",
      "provider": "signalk",
      "path": "environment.depth.belowTransducer",
      "unit": "m",
      "history": {"interval_ms": 60000, "points": 120, "reducer": "mean"}
    }
  ],
  "sections": [
    {
      "id": "navigation",
      "title": "Navigation",
      "button_labels": {
        "action_1": "MARK",
        "action_2": "MENU"
      },
      "sources": [
        {"id": "sog", "path": "navigation.speedOverGround", "unit": "m/s"}
      ],
      "pages": [
        {
          "id": "overview",
          "grid": {"columns": 3, "rows": 3, "gap": 4},
          "widgets": [
            {
              "type": "value",
              "source": "sog",
              "cell": {"column": 0, "row": 0, "column_span": 2, "row_span": 3},
              "label": "SOG",
              "display_unit": "kn",
              "decimals": 1,
              "max_digits": 6
            },
            {
              "type": "future-gauge",
              "source": "depth-history",
              "cell": {"column": 2, "row": 0}
            },
            {
              "type": "local-clock",
              "cell": {"column": 2, "row": 1}
            }
          ]
        }
      ]
    }
  ]
}
)json";

} // namespace

int main() {
    std::array<std::uint8_t, 4096> bytes{};
    const semantic_display::ConfigCompiler compiler{};
    const auto result = compiler.compile(configuration, bytes.data(), bytes.size());
    assert(result);

    const semantic_display::PackageView package{bytes.data(), result.packageSize};
    assert(package.valid());
    const auto info = package.info();
    assert(info.sourceCount == 2u);
    assert(info.sectionCount == 1u);
    assert(info.pageCount == 1u);
    assert(info.widgetCount == 3u);
    assert(info.historyBytes == semantic_display::historySeriesOverheadBytes +
                                120u * sizeof(float));

    semantic_display::RecordView record{};
    assert(package.first(record));
    unsigned unknownWidgets{};
    unsigned resolvedWidgets{};
    unsigned labelledSections{};
    do {
        semantic_display::SectionView section{};
        if (package.section(record, section) && section.showButtonLabels) {
            ++labelledSections;
            assert(section.previousPageLabel.empty());
            assert(section.nextPageLabel.empty());
            assert(section.action1Label == "MARK");
            assert(section.action2Label == "MENU");
        }
        semantic_display::WidgetView widget{};
        if (!package.widget(record, widget)) continue;
        if (widget.sourceIndex != semantic_display::noIndex) ++resolvedWidgets;
        if (widget.type == semantic_display::WidgetType::value) {
            assert(widget.maxDigits == 6u);
        }
        if (widget.type == semantic_display::WidgetType::unknown) {
            ++unknownWidgets;
            assert(widget.typeName == "future-gauge");
        }
    } while (package.next(record));
    assert(unknownWidgets == 1u);
    assert(resolvedWidgets == 2u);
    assert(labelledSections == 1u);

    constexpr auto invalid = R"json({
      "schema":"semantic-display/v1",
      "sections":[{"id":"bad","pages":[{
        "id":"bad","grid":{"columns":1,"rows":1},
        "widgets":[{"type":"value","source":"missing","cell":{"column":0,"row":0}}]
      }]}]
    })json";
    const auto invalidResult = compiler.compile(invalid, bytes.data(), bytes.size());
    assert(!invalidResult);
    assert(invalidResult.error == semantic_display::CompileError::unresolvedSource);

    const semantic_display::ConfigCompiler tinyHistory{{32768u, 4096u, 64u, 255u, 16u}};
    const auto historyResult = tinyHistory.compile(configuration, bytes.data(), bytes.size());
    assert(!historyResult);
    assert(historyResult.error == semantic_display::CompileError::historyBudgetExceeded);

    constexpr auto duplicate = R"json({
      "schema":"semantic-display/v1",
      "sections":[
        {"id":"same","pages":[{"id":"a","grid":{"columns":1,"rows":1},"widgets":[]}]},
        {"id":"same","pages":[{"id":"b","grid":{"columns":1,"rows":1},"widgets":[]}]}
      ]
    })json";
    const auto duplicateResult = compiler.compile(duplicate, bytes.data(), bytes.size());
    assert(!duplicateResult);
    assert(duplicateResult.error == semantic_display::CompileError::duplicateId);

    constexpr auto overlap = R"json({
      "schema":"semantic-display/v1",
      "sections":[{"id":"one","pages":[{
        "id":"a","grid":{"columns":2,"rows":1},"widgets":[
          {"type":"text","text":"A","cell":{"column":0,"row":0,"column_span":2}},
          {"type":"text","text":"B","cell":{"column":1,"row":0}}
        ]
      }]}]
    })json";
    const auto overlapResult = compiler.compile(overlap, bytes.data(), bytes.size());
    assert(!overlapResult);
    assert(overlapResult.error == semantic_display::CompileError::invalidLayout);

    const semantic_display::ConfigCompiler oneSource{{32768u, 4096u, 8192u, 255u, 16u, 1u}};
    const auto sourceResult = oneSource.compile(configuration, bytes.data(), bytes.size());
    assert(!sourceResult);
    assert(sourceResult.error == semantic_display::CompileError::dataBudgetExceeded);

    constexpr auto malformedUnknownProperty = R"json({
      "schema":"semantic-display/v1","future":not_json,
      "sections":[{"id":"one","pages":[{"id":"a","grid":{"columns":1,"rows":1},"widgets":[]}]}]
    })json";
    const auto malformedResult = compiler.compile(malformedUnknownProperty,
                                                   bytes.data(), bytes.size());
    assert(!malformedResult);
    assert(malformedResult.error == semantic_display::CompileError::invalidJson);
}
