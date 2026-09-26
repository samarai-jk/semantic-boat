# Display configuration v1

This document describes the provisional `semantic-display/v1` JSON format. The
machine-readable companion is [schema/display-v1.schema.json](schema/display-v1.schema.json),
and a larger example is [examples/remote-test-v1.json](examples/remote-test-v1.json).

## Complete small example

```json
{
  "schema": "semantic-display/v1",
  "permanent_sources": [
    {
      "id": "outside-temp",
      "path": "environment.outside.temperature",
      "unit": "K",
      "history": {
        "interval_ms": 60000,
        "points": 120,
        "reducer": "mean"
      }
    }
  ],
  "sections": [
    {
      "id": "navigation",
      "title": "NAVIGATION",
      "button_labels": {},
      "sources": [
        {
          "id": "sog",
          "path": "navigation.speedOverGround",
          "unit": "m/s",
          "stale_ms": 5000
        }
      ],
      "pages": [
        {
          "id": "overview",
          "grid": { "columns": 2, "rows": 1, "gap": 4 },
          "widgets": [
            {
              "type": "value",
              "source": "sog",
              "cell": { "column": 0, "row": 0 },
              "label": "SOG",
              "display_unit": "kn",
              "decimals": 1
            },
            {
              "type": "local-clock",
              "cell": { "column": 1, "row": 0 },
              "label": "LOCAL"
            }
          ]
        }
      ]
    }
  ]
}
```

Unknown properties are ignored so metadata and future optional fields do not
break an older compiler. Required semantics are still checked by the device.

## Sources and subscriptions

A source gives widgets and data connectors a stable local ID:

| Field | Meaning |
| --- | --- |
| `id` | Required identifier used by widgets. |
| `provider` | Connector name; defaults to `signalk`. The test config uses `fake`. |
| `path` | Required provider-specific data path. |
| `unit` | Unit of incoming numeric values. |
| `stale_ms` | Optional age after which the widget shows unavailable. Zero disables it. |
| `subscribe.period_ms` | Requested provider update period. Zero means provider default. |
| `history` | Optional RAM history policy described below. |

Sources in a section are subscribed only while that section is active. All of
the active section's sources remain subscribed while moving between its pages;
this avoids connection churn and lets the next page already have values.

`permanent_sources` are always subscribed. Use them sparingly for values whose
history must continue while another section is displayed. A section-local
source may use the same ID as a permanent source and takes precedence for
widgets in that section. Duplicate permanent IDs, or duplicate source IDs in
one section, are rejected.

The runtime does not contain Signal K, RS-485, CAN, or NMEA 2000 code. A
connector enumerates source records from `PackageView`, sends subscriptions for
the currently active set, and stores incoming values by `sourceIndex`.

## Sections, pages, and controls

Each section has an `id`, optional `title`, optional `sources`, and one or more
pages. Each page has an `id`, optional `title`, a grid, and widgets. Section IDs
must be globally unique; page IDs must be unique within their section.

The shared runtime exposes logical actions rather than physical button numbers:
previous/next section, previous/next page, and two application actions. A board
maps its available switches or directional pad onto those actions. Navigation
wraps at both ends.

The remote-a mapping is:

| Button | Action |
| --- | --- |
| B0 / B1 | Previous / next section; hold B0 for three seconds to sleep |
| B2 / B3 | Previous / next page |
| B4 | Action 1, currently reserved |
| B5 | Action 2: force a full display refresh |

After B0 has been held for one second, remote-a displays a two-second countdown
and asks the user to keep holding it. Releasing B0 cancels the countdown and
performs the normal previous-section action. At shutdown it sounds one long
beep. Once asleep, the retained E-paper image says `SLEEPING` and asks for any
button press. The MCU uses Stop 2, the RGB LED is off, and remote-a's USART2
receive path is suspended. That wake press is consumed without also performing
the button's normal action and produces three short beeps. The active section,
page, values, history, and application-modal state are retained. The system
clock and USART are restored, then the panel is reinitialized and fully redrawn
after wake.

An optional section-level `button_labels` object replaces the generic footer
with four captions aligned left-to-right over remote-a's B2, B3, B4, and B5
buttons. B2 and B3 automatically show the titles of the pages reached by the
previous/next actions. A section with only one page leaves both captions blank.
Long page names are shortened to eight characters plus a period so neighboring
zones cannot overlap.

The optional `action_1` and `action_2` strings name application actions, but a
caption is shown only when the firmware has installed an executable and
currently available `ActionHandler` for that action. A firmware-supplied action
label takes precedence over the configured label; remote-a uses this to expose
its permanent development `REFRESH` action even when an uploaded configuration
has no `button_labels`. A label in JSON alone does not imply behavior. When no
per-button labels or available actions exist, a larger generic previous
page/next page help line is shown. Up/down section controls are intentionally
not shown in this bottom-button footer.

## Grid and widgets

Grid coordinates are zero-based. `column_span` and `row_span` default to one.
Every widget must fit within its page and widget rectangles may not overlap.
There is no arbitrary-pixel positioning in v1.

Supported widgets are:

- `value`: shows a number or short string. It requires `source`; numeric values
  may set `display_unit` and `decimals`.
- `text`: shows static text and requires `text`.
- `local-clock`: asks the device's local RTC formatter for a value. It does not
  use Signal K and is intentionally distinct from a normal server-backed clock
  value.

Any other `type` compiles as an unknown widget. The runtime draws a placeholder
containing `UNKNOWN WIDGET` and the requested type name, while all supported
pages and widgets continue to operate.

Current built-in numeric conversions are `m/s` to `kn`, radians to degrees, and
kelvin to Celsius. A connector should keep incoming values in the source unit;
display conversion belongs to the widget/runtime.

## History

History is optional and RAM-only. A history declaration requires:

- `interval_ms`: output sample interval;
- `points`: number of reduced samples retained in a ring;
- `reducer`: `last`, `mean`, `minimum`, or `maximum` (defaults to `last`).

Incoming data may arrive much faster or slower than the history interval. The
reducer accumulates values during one interval and stores one float when the
next interval begins. The approximate configured RAM cost is 32 bytes plus four
bytes per point for each history source. Compilation fails if the sum exceeds
the device-provided history arena. History starts empty after boot and is not
written to EEPROM.

## Capacity and validation

There is intentionally no small arbitrary limit on section, page, or widget
counts. Their practical limit is the compiled-package byte budget (with a
16-bit format count as the distant format ceiling). Limits that correspond to
real fixed memory are supplied by each firmware in `CompileLimits`:

- input JSON bytes and compiled package bytes;
- total history arena bytes;
- source count, because each source requires a fixed `ValueSlot`;
- maximum string bytes and JSON nesting depth, because the no-heap parser uses
  bounded token and call storage.

The compiler returns a precise error instead of accepting a document that the
runtime cannot hold. The current remote-a composition allows up to 64 sources,
1,024 history bytes, 16,192 compiled-package bytes, 16,368 staged JSON bytes,
255 bytes per decoded string, and 16 JSON nesting levels. These are code
constants, not schema promises, and may be tuned for another MCU.

## Binary package and EEPROM

JSON is compiled once after upload. The binary package has a versioned header,
payload CRC, source-JSON CRC, counts, and a stream of length-prefixed source,
section, page, and widget records. All integers have an explicit little-endian
encoding and all relationships use indices, so the bytes can be loaded at any
address after reboot.

The default 32 KiB EEPROM layout is:

```text
slot A: 64-byte header + up to 16,192 package bytes
slot B: 64-byte header + up to 16,192 package bytes
settings: 256 bytes reserved at the end
```

The two slots provide atomic replacement and fallback. Settings are reserved
for future device-local preferences and are not part of the display JSON.

## Uploading through SWD

Build and flash firmware normally, then upload JSON from the repository root:

```bat
script\config.bat remote-a src\lib\semantic-display\examples\remote-test-v1.json
```

The script validates basic JSON syntax, wraps the exact UTF-8 bytes with a
length and CRC, and writes them to remote-a's reserved 16 KiB internal-flash
staging region at `0x0803C000`. It never overwrites the firmware region. On
reset, the device verifies the staging image, compiles it itself, and commits
the resulting package to EEPROM. If compilation fails, the newest valid EEPROM
package remains active and an acknowledgeable error modal is shown.

The staging JSON remains in internal flash. Its CRC prevents recompilation on
every boot once the corresponding EEPROM package has been committed. Set
`OPENOCD` and `OPENOCD_SCRIPTS` if OpenOCD is not installed at the defaults used
by the repository scripts.

The current remote-a development build has
`useDevelopmentDisplayConfiguration` enabled in its composition root. With no
valid SWD staging image it installs the embedded fake-data test configuration
and refreshes that package when the embedded JSON changes. Setting the constant
to false selects the small local-clock/no-config factory page for production.
