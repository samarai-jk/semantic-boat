#include "semantic_display/compiler.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <string>

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
        {"id": "sog", "path": "navigation.speedOverGround", "unit": "m/s", "outdated_ms": 10000, "stale_ms": 30000}
      ],
      "pages": [
        {
          "id": "overview",
          "history_plot": {"source":"depth-history","title":"DEPTH HISTORY","display_unit":"m","decimals":1,"y_min":-2.5,"y_max":1e2},
          "grid": {"columns": 3, "rows": 3, "gap": 4},
          "widgets": [
            {
              "type": "value",
              "source": "sog",
              "cell": {"column": 0, "row": 0, "column_span": 2, "row_span": 3},
              "label": "SOG",
              "display_unit": "kn",
              "decimals": 1,
              "max_digits": 6,
              "position_format": "DMS"
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
    semantic_display::HistoryPlotView plot{};
    assert(package.historyPlotByPage(0u, plot));
    assert(plot.sourceIndex == 0u && plot.sourceId == "depth-history");
    assert(plot.title == "DEPTH HISTORY" && plot.displayUnit == "m");
    assert(plot.fixedRange && plot.minimum == -2.5f && plot.maximum == 100.0f);
    const auto info = package.info();
    assert(info.sourceCount == 2u);
    assert(info.sectionCount == 1u);
    assert(info.pageCount == 1u);
    assert(info.widgetCount == 3u);
    semantic_display::SourceView source{};
    assert(package.sourceByIndex(0u, source) && source.outdatedAfterMs == 0u);
    assert(package.sourceByIndex(1u, source) && source.outdatedAfterMs == 10000u && source.staleAfterMs == 30000u);
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
          assert(widget.positionFormat == semantic_display::PositionFormat::dms);
        }
        if (widget.type == semantic_display::WidgetType::unknown) {
          assert(widget.positionFormat == semantic_display::PositionFormat::ddm);
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
    constexpr auto invalidAge = R"json({"schema":"semantic-display/v1","permanent_sources":[
      {"id":"position","path":"navigation.position","outdated_ms":10000,"stale_ms":10000}],
      "sections":[{"id":"one","pages":[{"id":"page","grid":{"columns":1,"rows":1},"widgets":[]}]}]})json";
    const auto invalidAgeResult = compiler.compile(invalidAge, bytes.data(), bytes.size());
    assert(!invalidAgeResult && invalidAgeResult.error == semantic_display::CompileError::invalidProperty);
    constexpr auto invalidPositionFormat = R"json({"schema":"semantic-display/v1","sections":[{"id":"one",
      "pages":[{"id":"page","grid":{"columns":1,"rows":1},"widgets":[
        {"type":"text","text":"bad","position_format":"invalid","cell":{"column":0,"row":0}}]}]}]})json";
    const auto invalidPositionResult = compiler.compile(invalidPositionFormat, bytes.data(), bytes.size());
    assert(!invalidPositionResult && invalidPositionResult.error == semantic_display::CompileError::invalidProperty);
    const auto rejectPlot = [&](const char* original, const char* replacement) {
      std::string document{configuration};
      const auto position = document.find(original);
      assert(position != std::string::npos);
      document.replace(position, std::strlen(original), replacement);
      assert(!compiler.compile(document, bytes.data(), bytes.size()));
    };
    rejectPlot("\"source\":\"depth-history\"", "\"source\":\"missing\"");
    rejectPlot("\"history\": {\"interval_ms\": 60000, \"points\": 120, \"reducer\": \"mean\"}", "\"subscribe\": {}");
    rejectPlot("\"y_min\":-2.5,", "");
    rejectPlot("\"y_max\":1e2", "\"y_max\":-10");
    rejectPlot("\"y_max\":1e2", "\"y_max\":1e100");
    rejectPlot("\"y_min\":-2.5,\"y_max\":1e2", "\"y_min\":-3e38,\"y_max\":3e38");
    rejectPlot("\"history_plot\": {", "\"history_plot\":{\"source\":\"depth-history\"},\"history_plot\": {");

    // Graphs with further values: each needs history over the same span.
    constexpr auto seriesConfiguration = R"json({"schema":"semantic-display/v1",
      "permanent_sources":[
        {"id":"speed","path":"environment.wind.speedTrue","unit":"m/s","history":{"interval_ms":30000,"points":56}},
        {"id":"direction","path":"environment.wind.directionTrue","unit":"rad","history":{"interval_ms":30000,"points":56}},
        {"id":"gust","path":"environment.wind.gust","unit":"m/s","history":{"interval_ms":30000,"points":56}},
        {"id":"short","path":"environment.wind.short","unit":"m/s","history":{"interval_ms":10000,"points":56}},
        {"id":"none","path":"environment.wind.none","unit":"m/s"}],
      "sections":[{"id":"wind","pages":[{"id":"wind","grid":{"columns":1,"rows":1},
        "history_plot":{"source":"speed","title":"WIND","display_unit":"kn","circular":false,
          "series":[{"source":"direction","title":"DIR","display_unit":"deg","decimals":0,"circular":true},
                    {"source":"gust","y_min":0,"y_max":20}]},
        "widgets":[{"type":"value","source":"speed","cell":{"column":0,"row":0}}]}]}]})json";
    const auto seriesResult = compiler.compile(seriesConfiguration, bytes.data(), bytes.size());
    assert(seriesResult);
    semantic_display::PackageView seriesPackage{bytes.data(), seriesResult.packageSize};
    semantic_display::HistoryPlotView seriesPlot{};
    assert(seriesPackage.historyPlotByPage(0u, seriesPlot));
    assert(seriesPlot.sourceIndex == 0u && !seriesPlot.circular && seriesPlot.extraCount == 2u);
    assert(seriesPlot.extra[0].sourceIndex == 1u && seriesPlot.extra[0].circular);
    assert(seriesPlot.extra[0].title == "DIR" && seriesPlot.extra[0].displayUnit == "deg" &&
           seriesPlot.extra[0].decimals == 0u && !seriesPlot.extra[0].fixedRange);
    assert(seriesPlot.extra[1].sourceIndex == 2u && seriesPlot.extra[1].fixedRange &&
           seriesPlot.extra[1].maximum == 20.0f);
    const auto rejectSeries = [&](const char* original, const char* replacement) {
      std::string document{seriesConfiguration};
      const auto position = document.find(original);
      assert(position != std::string::npos);
      document.replace(position, std::strlen(original), replacement);
      assert(!compiler.compile(document, bytes.data(), bytes.size()));
    };
    rejectSeries("{\"source\":\"gust\"", "{\"source\":\"short\"");
    rejectSeries("{\"source\":\"gust\"", "{\"source\":\"none\"");
    rejectSeries("{\"source\":\"gust\"", "{\"source\":\"missing\"");
    rejectSeries("{\"source\":\"gust\",", "{\"source\":\"gust\"},{\"source\":\"gust\",");
    rejectSeries("\"circular\":true", "\"circular\":1");
}
