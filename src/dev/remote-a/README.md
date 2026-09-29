# Remote-A firmware

## Device setup

Hold the Down button (`BTN1`) for one second to open **Setup**. A short press
continues to move to the next display section. Setup contains:

- **Settings** for device-local preferences;
- **Apps** for display applications/configurations offered by the server.

Selection dialogs use Up and Down to move, Previous or Action 1 to go back,
and Next or Action 2 to select.

The initial settings are:

- beep volume: **Min**;
- LED mode: **Normal**;
- minimum shown alert level: **Info**;
- developer mode: **Off**.

**Reset** performs an orderly software reset. The same operation is available
from anywhere while awake by pressing Up (`BTN0`) and Down (`BTN1`) together.
All reset causes use one shutdown service: it unsubscribes from server data,
closes the UART transport, silences feedback, puts the E-paper controller to
sleep, and only then resets the MCU. Future reset features should call that
service rather than resetting the MCU directly.

Beep volume applies to button, sleep, and wake cues. The passive buzzer is
driven from TIM15 PWM. Off disables ordinary tones; Min, Medium, and Max use
2%, 10%, and 50% output duty respectively. Perceived loudness is hardware and
frequency dependent, so these deliberately broad steps should be evaluated on
the assembled device. Once startup has completed successfully, the device plays
a short welcome beep at the configured ordinary volume.

Sleep state is stored with the device settings before the device enters sleep.
If power is removed while sleeping, the next boot skips the display, link,
services, and startup sound. It only flashes the blue channel of status LED0
at 1% duty for 100 ms and immediately enters Stop 2. Pressing any button
clears the stored sleep state and resets into the normal startup path. Older
settings records predate the sleep flag and are treated as awake.

Normal LED mode enables the green communication/activity indicator and drives
the RGB status LED at 1% duty. Subdued mode disables the activity indicator
(including button flashes) and drives the RGB status LED at 0.1% duty. Off mode
disables both normal indicators. Error/alarm state overrides all LED modes and
enables the red status LED at normal brightness. After an error or alarm dialog
has finished rendering, the device plays three short attention beeps at maximum
buzzer output regardless of the configured ordinary beep volume, so both can
override silent mode.
The communication LED remains an activity indicator and is not used for
alarms. An automatic day/night LED mode is intentionally deferred until server
data can provide the required ambient state.

Action 2 remains the full-display **Refresh** action in the factory application.

## Local metrics

When no downloaded application is stored, the factory application identifies
itself as **LOCAL DEFAULT** and shows local status, memory, firmware, display
configuration, and runtime-health pages. These values are ordinary display
sources with provider `device`; they are produced inside the firmware and are
never included in subscriptions sent to the server.

Available paths are:

- `system.uptime` and `system.cpu.clock`;
- `memory.flash.used`, `memory.flash.free`, and
  `memory.flash.utilization`;
- `memory.ram.static`, `memory.ram.headroom`, `memory.heap.used`, and
  `memory.stack.peak`;
- `memory.ram2.used` and `memory.ram2.free`;
- `display.config.bytes`, `display.history.bytes`, `display.sources`,
  `display.sections`, `display.pages`, `display.widgets`, and
  `display.partial.refreshes`;
- `communication.uart.dropped`, `communication.link.status`, and
  `system.events.dropped`.

Flash and RAM quantities are published in KiB. `memory.ram.static` is the
link-time `.data` plus `.bss` footprint in primary RAM. Heap and stack share
the remaining primary RAM. `memory.ram.headroom` is the untouched gap between
the current heap break and the deepest stack address observed since application
initialization. `memory.stack.peak` is a paint-pattern high-water measurement;
it includes configuration compilation, but not the early reset and CubeMX HAL
initialization that happen before the application starts monitoring.

`memory.heap.used` measures the newlib heap obtained through `_sbrk`. The
application currently uses fixed-capacity storage and does not intentionally
allocate from that heap, so it should normally remain zero. A future library
that calls `malloc` or `new` will make the value grow.

Metrics are sampled once per second but do not continuously request EPD
refreshes. Navigating to a page or using **Refresh** renders the latest sample;
a server-link state transition may request one immediate redraw.

Enabling **Developer mode** adds the compact status `Rnn.n Snn.n` to the right
side of the top bar on every application. `R` is the safe primary-RAM headroom
in KiB between the heap and the deepest observed stack address; `S` is the
observed peak stack usage in KiB. The measurement is allocation-free. Stack
painting is scanned only when Developer mode is enabled or the active page
subscribes to a local memory metric.

## Installing applications

The active compiled package occupies a fixed 8 KiB RAM2 buffer. Download and
compilation do not reserve another permanent RAM buffer: once a configuration
transfer begins, normal display rendering is paused and the 16.4 KiB E-paper
framebuffer is reused as separate 8 KiB JSON-input and compiled-output areas.
The old active package remains valid until the new package has been committed
to EEPROM. A successful commit uses the orderly reset path described above;
startup then loads the new package. This keeps installation-only memory out of
normal runtime while preserving the previous application on transfer, compile,
or storage failure.

## Signal K alerts

Incoming Info, Warning, Alarm, and Emergency notifications use a dedicated
full-screen presentation which pre-empts pages and setup dialogs. While the
framebuffer is exclusively reserved for a configuration download, alerts remain
queued and are presented after the reservation ends or the device reconnects
following installation. The implementation retains up to eight identified
notifications and deduplicates repeated protocol frames. Alarm behavior and the
four horizontal button actions are detailed in
[semantic-display/ALARMS.md](../../lib/semantic-display/ALARMS.md).

Under **Settings**, **Show alarm levels** selects the minimum level shown by
this device. Choosing **Emergency only** suppresses Info, Warning, and Alarm;
Emergency is always enabled. This preference is stored with the other device
settings in EEPROM.

## EEPROM layout

The AT24C256C uses 7-bit I2C address `0x52`. The compiled display-application
store occupies two large alternating slots. The final 256 bytes are reserved
for device setup. The settings store currently uses the first 128 bytes of that
reservation as two alternating 64-byte records; the remaining 128 bytes stay
reserved for future settings data.

Each settings record contains a version, generation, payload CRC, and commit
marker. The marker is written last. Startup selects the newest valid generation
and falls back to defaults if neither record is valid. Updating a preference
therefore leaves the previous generation recoverable if power is interrupted.

The protocol and server continue to call downloadable display applications
"configurations" internally. **Apps** is only the clearer user-facing name.
