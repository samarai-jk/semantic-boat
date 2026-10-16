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
          "history_plot": {
            "source": "outside-temp",
            "title": "OUTSIDE TEMPERATURE",
            "display_unit": "C",
            "decimals": 1,
            "y_min": -10,
            "y_max": 40
          },
          "grid": { "columns": 2, "rows": 1, "gap": 4 },
          "widgets": [
            {
              "type": "value",
              "source": "sog",
              "cell": { "column": 0, "row": 0 },
              "label": "SOG",
              "display_unit": "kn",
              "decimals": 1,
              "max_digits": 6
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
| `outdated_ms` | Optional earlier age at which the value is retained with an inverted `OLD` age badge. Zero disables the badge. |
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

For example, a position source can use:

```json
{"id":"position","provider":"signalk","path":"navigation.position","outdated_ms":10000,"stale_ms":60000}
```

These are both absolute ages, not consecutive delays: after 10 seconds the last
value remains visible with `OLD 10s`; at 60 seconds it becomes `--`. A longer
cutoff allows ages such as `OLD 3m` and `OLD 2h`. The badge shows whole seconds
below one minute, whole minutes below one hour, and whole hours thereafter.
When both thresholds are nonzero, `outdated_ms` must be less than `stale_ms`.
With `stale_ms: 0`, the retained value never expires. Omitting `outdated_ms`
preserves stale-only behavior. Explicit unavailable values still clear immediately.
Visible widgets age and expire without needing another incoming update; normal
render scheduling may delay a threshold transition by about a second.

The Semantic Boat gateway transports `navigation.position` as decimal latitude
and longitude with hemisphere letters. The display formats a `value` widget
using its optional `position_format`: `DD`, `DDM` (default), or `DMS`. Leave the
source unit and display unit empty. For example:

```json
{"type":"value","source":"position","position_format":"DDM","cell":{"column":0,"row":0}}
```

| Format | Example |
| --- | --- |
| DD | `48.858370, 2.294481` |
| DMS | 48&#176;51'30.1"N 2&#176;17'40.1"E |
| DDM | 48&#176;51.502'N 2&#176;17.669'E |

DD uses signed coordinates with six decimal places and a comma separator,
without hemisphere letters. DMS uses degree symbols, minute/second marks,
hemisphere letters, and one decimal place for seconds. DDM uses degree symbols,
minute marks, hemisphere letters, and three decimal places for minutes. No
leading zeros are added to degrees, minutes, or whole seconds. Decimal fractions
retain their specified number of places.

DD stays on one line; DMS/DDM use two lines where widget height permits. Text
reduces its font scale to fit. Allow enough columns for the longer forms. The
gateway transports six decimal places; changing notation does not increase
underlying measurement accuracy or recover precision from older cached values.
Rounding carries correctly between seconds, minutes, and degrees. Existing
compiled widgets without the setting default to DDM on updated firmware.
Choosing another format requires saving and reinstalling the configuration.
Other arbitrary Signal K object values are not supported.

Updated remote-a firmware accepts an optional source-record age extension and
requests timestamp-aware link values. Legacy compiled source records remain
readable. Reinstall a configuration after upgrading firmware to compile the
new `outdated_ms` field; old firmware ignores that JSON field. Where supplied,
Signal K sample timestamps contribute to age, so cached values do not restart
fresh on reconnect. Without a usable provider timestamp, age starts at delivery.

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
| B4 | Action 1: toggle GRAPH / VALUES when the page assigns a history plot |
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
not shown in this bottom-button footer. When multiple sections exist, the right
edge of the top bar shows an up arrow when a logically previous section exists,
and the right edge of the bottom bar shows a down arrow when a logically next
section exists. The first and last sections therefore show only one arrow even
though section navigation itself wraps around.

## Grid and widgets

Grid coordinates are zero-based. `column_span` and `row_span` default to one.
Every widget must fit within its page and widget rectangles may not overlap.
There is no arbitrary-pixel positioning in v1.

Supported widgets are:

- `value`: shows a number or short string. It requires `source`; numeric values
  may set `display_unit`, `decimals`, and `max_digits`. `max_digits` is the
  maximum formatted character count, including a minus sign and decimal point,
  and defaults to 6. The renderer chooses a fixed bitmap-font scale that fits
  that many characters inside the widget. The scale therefore stays unchanged
  as live values change. Values longer than the configured maximum are clipped
  with a trailing dot rather than changing font size.
- `text`: shows static text and requires `text`.
- `local-clock`: asks the device's local RTC formatter for a value. It does not
  use Signal K and is intentionally distinct from a normal server-backed clock
  value.

Any other `type` compiles as an unknown widget. The runtime draws a placeholder
containing `UNKNOWN WIDGET` and the requested type name, while all supported
pages and widgets continue to operate.

Current built-in numeric conversions are `m/s` to `kn`, radians to degrees,
kelvin to Celsius, metres to nautical miles, pascals to hectopascals or bar,
hertz to RPM, seconds to hours, ratio to percent, and cubic metres per second
to litres per hour. A connector should keep incoming values in the source
unit; display conversion belongs to the widget/runtime.

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

### Page History Plot

Each page may assign one numeric history source using `history_plot`, independently
of its widgets. B4, the third button in the bottom row, toggles between the normal
page and a full-page line graph. Its label changes from `GRAPH` to `VALUES`.
Navigating to another page or section returns to normal mode. Pages without a
plot retain their existing Action 1 behavior; B5 still refreshes the display.

| Field | Meaning |
| --- | --- |
| `source` | Required source ID with a non-empty source-level `history` policy. Section-local sources override permanent sources. |
| `title` | Optional graph heading; defaults to the source ID. |
| `display_unit` | Optional numeric conversion; defaults to the source unit. |
| `decimals` | Y-axis label precision, 0 through 6; defaults to 1. |
| `y_min`, `y_max` | Optional fixed bounds in **display units**, supplied together with minimum less than maximum. Omit both for zero-based automatic scaling. |

Automatic scaling starts at zero and uses the largest retained value for the
upper bound. Zero or negative-only histories use a range of 0 to 1. Configure a
negative `y_min` and matching `y_max` to show negative values rather than clipping
them to zero. Fixed ranges clip out-of-range values at the graph edges. Empty
histories show `NO DATA`. Graph traces are two pixels thick; there are no axis
strokes or dedicated Y-label gutter. The white content area is the plot, with a
six-pixel outer inset. The 14-pixel title is centered at the top, with the
duration at the top right. The plotted maximum and its scale/unit label begin
below that header row. Annotations use 11-pixel text. Small white backdrops
keep those annotations readable where the trace crosses them. Plot assignments
reuse the source's ring rather than allocating
another series; multiple pages can plot the same source with different units or
axes. Text values, including the `signalk-text` provider, are not numeric history.

The X-axis is oldest to latest, with a nominal span of
`(points - 1) * interval_ms`. There are no labels below the X-axis. Its nominal
duration appears in the top-right corner as `30m` or `10h`, rounded to the nearest
minute or hour (seconds or milliseconds for shorter windows).
Partially filled rings are right-aligned. History
does not store timestamps or insert empty buckets for outages, so this is a
sample-history graph, not an exact wall-clock timeline. Section-local sampling
pauses outside that section; use permanent sources for continuous collection.
Graph redraws follow committed history samples rather than every raw update.
All samples disappear on reboot or configuration replacement.

Remote-a reserves 1,024 history bytes in total: a 120-point source costs 512
bytes, so two such sources fit. The designer exposes source interval, points and
reducer, page plot assignment, heading, units, precision and fixed bounds. The
catalog and device compiler enforce the storage budget. Updated display firmware
is required; saving a designer draft does not update or flash a device.

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
1,024 history bytes, 8,192 compiled-package bytes, 16,368 staged JSON bytes,
255 bytes per decoded string, and 16 JSON nesting levels. These are code
constants, not schema promises, and may be tuned for another MCU.

## Binary package and EEPROM

JSON is compiled once after upload. The binary package has a versioned header,
payload CRC, source-JSON CRC, counts, and a stream of length-prefixed source,
section, page, widget, and optional history-plot records. All integers have an explicit little-endian
encoding and all relationships use indices, so the bytes can be loaded at any
address after reboot.

The default 32 KiB EEPROM layout is:

```text
slot A: 64-byte header + package bytes
slot B: 64-byte header + package bytes
settings: 256 bytes reserved at the end
```

The two slots provide atomic replacement and fallback. Settings are reserved
for device-local preferences and are not part of the display JSON. The physical
slot capacity is 16,192 bytes, while the current Remote-A runtime deliberately
limits compiled packages to 8,192 bytes so half of RAM2 remains available to
future firmware features.

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

The current remote-a build has `useDevelopmentDisplayConfiguration` disabled
in its composition root. With no valid EEPROM package or SWD staging image it
installs the unmistakable local-clock/status default page. The same built-in
configuration is always available as `DEFAULT (LOCAL)` in configuration
selection.
