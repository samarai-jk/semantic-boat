# semantic-display

`semantic-display` is the portable, allocation-free configuration and UI runtime
for Semantic Boat display devices. It turns a human-editable JSON document into
a compact validated package, keeps live values and optional history in
caller-owned memory, and renders the selected page through `slstm32`'s E-paper
interfaces.

The library is deliberately independent of a particular board and data bus. An
RS-485 Signal K connector, a CAN/NMEA 2000 connector, and a host test connector
can all feed the same data store and use the same compiled display package.

The version-1 schema and widget set are an initial proposal. Package and schema
versions are explicit so they can evolve without silently interpreting old data
differently.

## Main pieces

- `ConfigCompiler` parses JSON directly from a `string_view` into a
  caller-provided byte buffer. It uses no heap and retains no JSON tree.
- `PackageView` validates and reads the resulting little-endian record package.
  The package contains offsets and indices, never process pointers, so it is
  safe to store in EEPROM.
- `DataStore` owns one fixed-size value slot per configured source. A slot may
  contain a number, a short string, or an unavailable state.
- `HistoryStore` uses a caller-provided byte arena for selected numeric sources.
  It stores reduced interval samples, not every incoming update.
- `DisplayService` owns section/page navigation, widgets, modal overlays,
  coalesced redraws, stale-value handling, and E-paper refresh policy.
- `ConfigStore` commits compiled packages to two EEPROM slots. The new slot is
  marked valid only after its payload is written; loading can fall back to the
  older slot after an interrupted or corrupt update.

Detailed configuration syntax is in [CONFIGURATION.md](CONFIGURATION.md). Panel
transfer behavior and the safe E-paper cancellation boundary are documented in
[../slstm32/EPD.md](../slstm32/EPD.md).

## Dependency direction

```text
device UI and data connector
            |
            v
semantic-display runtime/compiler/storage
            |
            v
slstm32 services, E-paper interfaces, and byte storage
            |
            v
device-owned STM32 HAL bindings
```

Keep protocol-specific parsing and subscription messages in the device or a
separate connector library. Keep page selection, config semantics, history
reduction, and generic widgets here.

## Integrating a device

The device supplies all storage, which makes RAM costs visible in its
composition root:

```cpp
std::array<std::uint8_t, 8192> packageBytes;
std::array<std::uint8_t, 2048> historyBytes;
std::array<semantic_display::ValueSlot, 32> values;

semantic_display::DataStore data{values.data(), values.size()};
semantic_display::HistoryStore history{historyBytes.data(), historyBytes.size()};

semantic_display::ConfigCompiler compiler{{
    16368,                    // maximum input JSON bytes
    packageBytes.size(),      // maximum compiled package bytes
    historyBytes.size(),      // maximum requested history RAM
    255,                      // maximum UTF-8 bytes in one string
    16,                       // maximum JSON nesting depth
    values.size(),            // maximum sources/value slots
}};

const auto result = compiler.compile(json, packageBytes.data(), packageBytes.size());
if (!result) {
    // result.error, result.inputOffset, and result.message explain the failure.
}
```

After compilation, construct a `PackageView`, give it to `DisplayService`, and
initialize the normal `slstm32::Application`. The display service configures
the data and history stores from the package during `init()`.

Connectors publish by source index:

```cpp
if (data.setNumber(sourceIndex, value, nowMs)) {
    display.sourceUpdated(sourceIndex);
}
```

Use `sourceIsSubscribed(package, sourceIndex, display.activeSection())` when
deciding which inputs to request or publish. It returns true for permanent
sources and for sources owned by the active section. A real connector should
rebuild its server subscriptions when `NavigationNotification` reports a
section change. Page changes do not change the subscription set.

## Runtime behavior

Input changes update navigation immediately. Rendering is delayed briefly so a
burst of button presses coalesces into the newest page, and an E-paper upload is
cancelled only before its physical refresh commit point. Inputs received during
an unavoidable physical refresh still change the model and cause a later
render.

`DisplayPolicy::minimumDataRenderIntervalMs` limits redraw frequency.
`fullRefreshAfterPartialUpdates` is a count of completed partial refreshes; zero
disables periodic full refreshes. Startup still uses a full refresh.
`localClockRenderIntervalMs` controls local-clock redraws and may also be zero
to disable automatic clock refreshes.

`requestFullRefresh()` schedules an on-demand full waveform without blocking
the caller. `requestSleep()` first lets any already-committed physical refresh
finish, then powers down the panel. `wake()` reinitializes the panel and queues
a full redraw while retaining the current UI model and navigation state. The
device remains responsible for MCU low-power entry, wake sources, and status
indication; those policies are deliberately outside this portable library.

Application actions are installed through `ActionHandler`. Its optional label
callback can give a firmware-defined action a stable footer title independent
of the uploaded display configuration.

An unknown widget type remains in the package and renders an `UNKNOWN WIDGET`
placeholder with its type name. This lets the remainder of a newer or partially
supported configuration continue to work. Structurally invalid JSON, bad source
references, duplicate IDs in the same scope, out-of-grid widgets, and
overlapping widget cells are compile errors.

Modals are runtime UI, not config widgets. `showModal()` displays an
acknowledgeable information, warning, error, or alarm overlay. The optional
`ModalNotification` callback lets device policy drive an LED or buzzer without
putting hardware behavior in this library.

`showTransientModal()` provides a device-owned temporary overlay for status
such as a shutdown countdown. It is separate from the application modal, so
dismissing the temporary overlay restores any underlying alarm or error. A
transient modal may supply its own bottom prompt.

## Host tests

```sh
cmake -S src/lib/semantic-display -B build/semantic-display-host \
  -DBUILD_TESTING=ON
cmake --build build/semantic-display-host
ctest --test-dir build/semantic-display-host --output-on-failure
```

The tests cover compiler validation, unknown widgets, navigation, source
scoping, history reducers, modal dismissal, explicit full refresh, display
sleep/wake state retention, and power-loss/corruption fallback in the two-slot
configuration store. A host test does not replace an Arm build or target
hardware test.
