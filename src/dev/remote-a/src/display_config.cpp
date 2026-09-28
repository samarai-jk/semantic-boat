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
    "sources":[
      {"id":"uptime","provider":"device","path":"system.uptime","unit":"s"},
      {"id":"cpu-clock","provider":"device","path":"system.cpu.clock","unit":"MHz"},
      {"id":"flash-used","provider":"device","path":"memory.flash.used","unit":"KiB"},
      {"id":"flash-free","provider":"device","path":"memory.flash.free","unit":"KiB"},
      {"id":"flash-util","provider":"device","path":"memory.flash.utilization","unit":"%"},
      {"id":"ram-static","provider":"device","path":"memory.ram.static","unit":"KiB"},
      {"id":"ram-headroom","provider":"device","path":"memory.ram.headroom","unit":"KiB"},
      {"id":"heap-used","provider":"device","path":"memory.heap.used","unit":"KiB"},
      {"id":"stack-peak","provider":"device","path":"memory.stack.peak","unit":"KiB"},
      {"id":"ram2-used","provider":"device","path":"memory.ram2.used","unit":"KiB"},
      {"id":"ram2-free","provider":"device","path":"memory.ram2.free","unit":"KiB"},
      {"id":"config-size","provider":"device","path":"display.config.bytes","unit":"KiB"},
      {"id":"history-used","provider":"device","path":"display.history.bytes","unit":"KiB"},
      {"id":"source-count","provider":"device","path":"display.sources","unit":""},
      {"id":"section-count","provider":"device","path":"display.sections","unit":""},
      {"id":"page-count","provider":"device","path":"display.pages","unit":""},
      {"id":"widget-count","provider":"device","path":"display.widgets","unit":""},
      {"id":"partial-count","provider":"device","path":"display.partial.refreshes","unit":""},
      {"id":"uart-dropped","provider":"device","path":"communication.uart.dropped","unit":""},
      {"id":"event-dropped","provider":"device","path":"system.events.dropped","unit":""},
      {"id":"link-status","provider":"device","path":"communication.link.status","unit":""}
    ],
    "pages":[
      {
        "id":"status","title":"LOCAL STATUS","grid":{"columns":2,"rows":2,"gap":4},
        "widgets":[
          {"type":"local-clock","cell":{"column":0,"row":0},"label":"LOCAL TIME"},
          {"type":"value","source":"uptime","cell":{"column":1,"row":0},"label":"UPTIME","display_unit":"h","decimals":1,"max_digits":7},
          {"type":"value","source":"link-status","cell":{"column":0,"row":1},"label":"SERVER LINK","max_digits":8},
          {"type":"value","source":"cpu-clock","cell":{"column":1,"row":1},"label":"CPU CLOCK","display_unit":"MHz","decimals":0,"max_digits":3}
        ]
      },
      {
        "id":"memory","title":"MEMORY","grid":{"columns":2,"rows":3,"gap":4},
        "widgets":[
          {"type":"value","source":"ram-headroom","cell":{"column":0,"row":0},"label":"RAM HEADROOM","display_unit":"KiB","decimals":1,"max_digits":5},
          {"type":"value","source":"ram-static","cell":{"column":1,"row":0},"label":"RAM STATIC","display_unit":"KiB","decimals":1,"max_digits":5},
          {"type":"value","source":"stack-peak","cell":{"column":0,"row":1},"label":"STACK PEAK","display_unit":"KiB","decimals":1,"max_digits":5},
          {"type":"value","source":"heap-used","cell":{"column":1,"row":1},"label":"HEAP USED","display_unit":"KiB","decimals":1,"max_digits":5},
          {"type":"value","source":"ram2-used","cell":{"column":0,"row":2},"label":"RAM2 USED","display_unit":"KiB","decimals":1,"max_digits":5},
          {"type":"value","source":"ram2-free","cell":{"column":1,"row":2},"label":"RAM2 FREE","display_unit":"KiB","decimals":1,"max_digits":5}
        ]
      },
      {
        "id":"firmware","title":"FIRMWARE","grid":{"columns":2,"rows":2,"gap":4},
        "widgets":[
          {"type":"value","source":"flash-used","cell":{"column":0,"row":0},"label":"FLASH USED","display_unit":"KiB","decimals":1,"max_digits":6},
          {"type":"value","source":"flash-free","cell":{"column":1,"row":0},"label":"FLASH FREE","display_unit":"KiB","decimals":1,"max_digits":6},
          {"type":"value","source":"flash-util","cell":{"column":0,"row":1},"label":"FLASH USED","display_unit":"%","decimals":1,"max_digits":5},
          {"type":"text","cell":{"column":1,"row":1},"text":"LOCAL DEFAULT\nNO SERVER CONFIG"}
        ]
      },
      {
        "id":"display","title":"DISPLAY CONFIG","grid":{"columns":2,"rows":3,"gap":4},
        "widgets":[
          {"type":"value","source":"config-size","cell":{"column":0,"row":0},"label":"COMPILED CONFIG","display_unit":"KiB","decimals":1,"max_digits":5},
          {"type":"value","source":"history-used","cell":{"column":1,"row":0},"label":"HISTORY RAM","display_unit":"KiB","decimals":1,"max_digits":5},
          {"type":"value","source":"source-count","cell":{"column":0,"row":1},"label":"SOURCES","decimals":0,"max_digits":3},
          {"type":"value","source":"section-count","cell":{"column":1,"row":1},"label":"SECTIONS","decimals":0,"max_digits":3},
          {"type":"value","source":"page-count","cell":{"column":0,"row":2},"label":"PAGES","decimals":0,"max_digits":3},
          {"type":"value","source":"widget-count","cell":{"column":1,"row":2},"label":"WIDGETS","decimals":0,"max_digits":3}
        ]
      },
      {
        "id":"health","title":"RUNTIME HEALTH","grid":{"columns":2,"rows":2,"gap":4},
        "widgets":[
          {"type":"value","source":"uart-dropped","cell":{"column":0,"row":0},"label":"UART DROPPED","decimals":0,"max_digits":7},
          {"type":"value","source":"event-dropped","cell":{"column":1,"row":0},"label":"EVENTS DROPPED","decimals":0,"max_digits":7},
          {"type":"value","source":"partial-count","cell":{"column":0,"row":1},"label":"PARTIAL REFRESHES","decimals":0,"max_digits":7},
          {"type":"text","cell":{"column":1,"row":1},"text":"LOCAL DEFAULT"}
        ]
      }
    ]
  }]
})json";

} // namespace

std::string_view testDisplayConfiguration() { return testConfiguration; }
std::string_view factoryDisplayConfiguration() { return factoryConfiguration; }

} // namespace remote_a
