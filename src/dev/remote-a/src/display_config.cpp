#include "display_config.hpp"

namespace remote_a {
namespace {

constexpr auto testConfiguration = R"json({
  "schema":"semantic-display/v1",
  "name":"Remote test",
  "permanent_sources":[
    {"id":"outside-temp","provider":"signalk","path":"environment.outside.temperature","unit":"K",
     "subscribe":{"period_ms":1000},
     "history":{"interval_ms":60000,"points":120,"reducer":"mean"}}
  ],
  "sections":[
    {
      "id":"navigation","title":"NAVIGATION",
      "button_labels":{"action_2":"REFRESH"},
      "sources":[
        {"id":"sog","provider":"signalk","path":"navigation.speedOverGround","unit":"m/s","stale_ms":5000},
        {"id":"cog","provider":"signalk","path":"navigation.courseOverGroundTrue","unit":"rad","stale_ms":5000},
        {"id":"depth","provider":"signalk","path":"environment.depth.belowTransducer","unit":"m","stale_ms":10000}
      ],
      "pages":[
        {
          "id":"overview","title":"OVERVIEW","grid":{"columns":3,"rows":3,"gap":4},
          "widgets":[
            {"type":"value","source":"sog","cell":{"column":0,"row":0,"column_span":2,"row_span":3},"label":"SOG","display_unit":"kn","decimals":1},
            {"type":"value","source":"cog","cell":{"column":2,"row":0},"label":"COG","display_unit":"deg","decimals":0},
            {"type":"value","source":"depth","cell":{"column":2,"row":1},"label":"DEPTH","display_unit":"m","decimals":1},
            {"type":"local-clock","cell":{"column":2,"row":2},"label":"LOCAL"}
          ]
        },
        {
          "id":"course","title":"COURSE","grid":{"columns":2,"rows":2,"gap":4},
          "widgets":[
            {"type":"value","source":"cog","cell":{"column":0,"row":0,"column_span":2},"label":"COURSE OVER GROUND","display_unit":"deg","decimals":0},
            {"type":"value","source":"sog","cell":{"column":0,"row":1},"label":"SPEED","display_unit":"kn","decimals":1},
            {"type":"value","source":"depth","cell":{"column":1,"row":1},"label":"DEPTH","display_unit":"m","decimals":1}
          ]
        },
        {
          "id":"future","title":"FUTURE WIDGET","grid":{"columns":1,"rows":1,"gap":0},
          "widgets":[
            {"type":"wind-compass","cell":{"column":0,"row":0}}
          ]
        }
      ]
    },
    {
      "id":"system","title":"SYSTEM",
      "button_labels":{"action_2":"REFRESH"},
      "sources":[
        {"id":"voltage","provider":"signalk","path":"electrical.batteries.house.voltage","unit":"V","stale_ms":5000}
      ],
      "pages":[
        {
          "id":"status","title":"DEVICE STATUS","grid":{"columns":2,"rows":2,"gap":4},
          "widgets":[
            {"type":"text","cell":{"column":0,"row":0,"column_span":2},"text":"SEMANTIC BOAT REMOTE"},
            {"type":"local-clock","cell":{"column":0,"row":1},"label":"DEVICE TIME"},
            {"type":"value","source":"voltage","cell":{"column":1,"row":1},"label":"HOUSE","display_unit":"V","decimals":1}
          ]
        },
        {
          "id":"environment","title":"ENVIRONMENT","grid":{"columns":2,"rows":1,"gap":4},
          "widgets":[
            {"type":"value","source":"outside-temp","cell":{"column":0,"row":0},"label":"OUTSIDE","display_unit":"C","decimals":1},
            {"type":"history-graph","source":"outside-temp","cell":{"column":1,"row":0}}
          ]
        }
      ]
    }
  ]
})json";

constexpr auto factoryConfiguration = R"json({
  "schema":"semantic-display/v1",
  "name":"Local default",
  "sections":[{
    "id":"local-default","title":"LOCAL DEFAULT",
    "button_labels":{"action_2":"REFRESH"},
    "pages":[{
      "id":"default","title":"DEFAULT CONFIG","grid":{"columns":1,"rows":3,"gap":4},
      "widgets":[
        {"type":"text","cell":{"column":0,"row":0},"text":"LOCAL DEFAULT"},
        {"type":"local-clock","cell":{"column":0,"row":1},"label":"LOCAL TIME"},
        {"type":"text","cell":{"column":0,"row":2},"text":"NO SERVER CONFIG"}
      ]
    }]
  }]
})json";

} // namespace

std::string_view testDisplayConfiguration() { return testConfiguration; }
std::string_view factoryDisplayConfiguration() { return factoryConfiguration; }

} // namespace remote_a
