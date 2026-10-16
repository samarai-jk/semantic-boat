# Semantic Boat Devices

Signal K plugin with independent device sessions, preferred-source live data,
and one shared catalog of `semantic-display/v1` configurations. The current
adapters support point-to-point links and a shared, master-polled RS485 bus with
independent addressed displays. Values are forwarded in Signal K SI units by default; display conversion belongs to
the configuration. Missing, null, unsupported, and non-finite values are sent
as unavailable, never replaced by synthetic values.

The opt-in `signalk-text` source provider uses the same preferred Signal K paths,
but sends numbers as compact text with six significant digits and booleans as
`YES`/`NO`. Units remain SI; numeric display conversions do not apply to text.
This allows existing display firmware to show tiny particulate concentrations
in scientific notation instead of rounding them to zero.

For particulate sources in `kg/m3`, the opt-in `signalk-ugm3` provider sends
numeric values multiplied by 1e9. Use `ug/m3` for both source and widget units.
This conversion affects only the display link, not Signal K data or other
subscribers, and works without updating display firmware.

## Installation

From the Signal K configuration directory:

```sh
npm install /path/to/semantic-boat/src/signalk/plugin/semantic-boat-devices
```

Enable **Semantic Boat Devices** in the Signal K plugin configuration and
restart the server. Open `/plugins/semantic-boat-devices/ui/` on that server.
The Signal K Web Apps launcher also serves the page at `/@semantic-boat/devices/`;
both entry points use the same plugin API and configuration catalog.
The Devices view shows active subscriptions, exact path mappings, preferred
source/value, serial counters, and configuration-transfer status. Configurations
provides a template-based property/JSON editor, schema validation, save/delete/download,
and a catalog shared by all configured devices. Select a catalog entry in the
Devices view to install it, or use **Apps** on the display.

Example plugin configuration (the address must match the device's Settings):

```json
{
  "buses": [{
    "id": "rs485-main",
    "type": "rs485-addressed",
    "path": "/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_BG03SD1E-if00-port0",
    "baudRate": 115200
  }],
  "devices": [{
    "id": "remote-a-1", "label": "Remote-A 001",
    "bus": "rs485-main", "address": 1
  }, {
    "id": "remote-a-2", "label": "Remote-A 002",
    "bus": "rs485-main", "address": 2
  }],
  "mappings": [
    {"from": "propulsion.main.revolutions", "to": "propulsion.port.revolutions"},
    {"from": "propulsion.main.coolantTemperature", "to": "propulsion.port.temperature"},
    {"from": "propulsion.main.oilPressure", "to": "propulsion.port.oilPressure"},
    {"from": "propulsion.main.alternatorVoltage", "to": "propulsion.port.alternatorVoltage"},
    {"from": "propulsion.main.fuel.rate", "to": "propulsion.port.fuel.rate"},
    {"from": "propulsion.main.runTime", "to": "propulsion.port.runTime"}
  ]
}
```

Mappings are optional compatibility aliases, not unit conversions. They let an
older installed configuration requesting `propulsion.main.revolutions` use this
server's `propulsion.port.revolutions` without reinstalling the display app.
They do not change Signal K values or source priorities. New configurations
should use actual Signal K paths directly; all bundled templates do so and
need no mappings. Keep legacy aliases only while older installed apps need them.
An addressed bus uses the configured address on the wire and opens its serial
port once. Device IDs remain stable plugin identifiers; an address is not a
hardware identity or authentication. For `serial-point-to-point`, addresses
remain administrative metadata and the bus accepts only one device.

Upgrade each display separately with the RS485 build, set local addresses 001
and 002, and only then attach both to the shared pair. Do not mix legacy RS485
firmware with the polled bus. The current addressed adapter requires 115200 baud.

## Behavior And Limits

- Subscriptions replace atomically only after a complete snapshot. Hello,
  disconnect, timeout, and stop release all previous subscriptions.
- Values use Signal K's preferred-source lookup, not whichever source most
  recently delivered a delta. Updates are coalesced per source and respect
  the requested period (default 500 ms, minimum 100 ms).
- Value frames have a 250 ms processing gap; queued values coalesce per source.
  Control/configuration frames are paced 30 ms apart, with a bounded 256-frame queue. Reconnect
  retries use the stable serial path. Pings detect silent session loss.
- Gateway writes wait for 50 ms of receive silence. An incomplete subscription
  snapshot gets up to 3 seconds of receive silence before gateway retries;
  Hello and ping traffic do not interrupt an active snapshot.
- On an addressed bus one master schedules request/reply turns round robin.
  Each request grants one device one reply, retries replay cached responses,
  and missing devices back off without stopping the other session. Bus reply,
  timeout, unexpected-reply and decode counters are available in `/state`.
- The management page refreshes the catalog independently of templates and
  retries after temporary loading failures. Configurations are managed in the
  plugin's web page, not in Signal K's bus/device settings form.
- Catalog files live under the server-provided plugin data directory in
  `configurations/`; they are saved atomically and are not device-specific.
- Maximum 16 files, 47-byte filenames, 8192-byte compact JSON per configuration.
  JSON Schema validation is preliminary; the firmware compiler is authoritative.
- Transfers use 32-byte chunks and CRC-32. `sent-awaiting-device-restart` means
  bytes were sent, not that installation succeeded: v1 has no install ACK.
- The left catalog is the only configuration selector. New configuration opens
  a filename/template dialog (including a blank draft), separate from selection.
- Properties follow the document hierarchy: configuration properties first,
  sections in a horizontal strip, then a page list beside the selected page's
  grid and widget properties. Add/delete controls live at their respective level.
  Expand Permanent sources or Section sources to edit bindings and timing.
- The Properties editor selects sections/pages and edits widget labels, types,
  sources, display units, precision, cell positions/spans and grid dimensions.
  Its clickable layout is a placement preview, not a pixel-exact firmware render.
  Add/delete controls manage sections, pages, widgets and sources; the final
  section/page and sources used by widgets are protected from deletion.
- Each page can select one History source. Expand the source panel and enable
  Record history to set its interval, retained points and reducer. The designer
  shows the shared 1024-byte history RAM budget; 120 points cost 512 bytes.
  Set the graph title, display unit, decimals and optional fixed Y bounds on the
  page. Updated remote-a firmware uses B4 (third bottom button) to toggle
  GRAPH / VALUES; page navigation resets to values. Plot sources are protected
  from deletion or disabling history while assigned. Numeric samples are RAM-only;
  use permanent sources to collect across sections. See the
  [configuration contract](../../../lib/semantic-display/CONFIGURATION.md#page-history-plot)
  for nominal-time-axis and scaling details. Save and explicitly install the
  configuration after upgrading firmware; no graph widget type is needed.
- Navigation examples assign SOG to Overview, STW to Motion and minimum depth
  below keel to Depth References. Three permanent 72-point histories sample
  every 10 seconds and use 960 bytes. Weather examples assign peak wind to Wind,
  temperature to Air, peak PM2.5 to Particles and pressure to a Pressure page.
  Four permanent 56-point histories use 1024 bytes; wind samples every 10 seconds
  and the other series every minute. These demonstrate both fixed and automatic
  Y ranges while retaining the normal value pages.
- Signal K path suggestions come from the server's current self-vessel tree and
  include numeric/text values and unit metadata. Refresh picks up new providers;
  custom paths remain editable even when no provider has published them yet.
  Source timing and SI units are editable separately from widget display units.
- Arrow controls reorder the selected section or page while preserving its
  selection, IDs, sources, widgets, and custom fields. Moves update the draft;
  save and explicitly install it to change the order on a display.
- Sources have separate Outdated after and Stale after fields in milliseconds.
  Outdated values remain visible with an inverted age badge until the final
  stale cutoff. Use 10000 and 60000 for a 10-second warning and 60-second cutoff.
  This requires updated display firmware and reinstalling the configuration.
- `navigation.position` supports per-widget DD, DDM, and DMS formats; the default
  is DDM. Choose Position format on a position widget and leave both units blank.
  Updated firmware uses separate coordinate lines where space permits. Save and
  reinstall to change the format; legacy compiled widgets default to DDM on the
  updated firmware. Other arbitrary object-valued paths remain unsupported.
- Properties and JSON edit the same document and preserve fields not exposed by
  the property grid, including history, alarms and custom extensions. Saving a
  catalog configuration never automatically installs it on a device.
- The gateway is read-only with respect to Signal K data. Alert forwarding,
  alert-action handling, and actuator commands are not implemented in this phase.

## Sensor Commissioning

The [sound meter commissioning notes](docs/sound-meter.md) contain the verified
Modbus settings, measurement conversion, supplied wiring/specifications, and
limits of the discovery. Sensor decoding/publishing is not implemented by the
current display adapters.

For the upcoming weather station test:

- Wait for confirmation that only the powered weather station is connected;
  disconnect the sound meter and both displays from the probe channel.
- Confirm supply voltage, polarity, RS485 wiring, and the actual serial path.
  Do not infer weather station power requirements from the sound meter.
- Keep the display gateway paused and check exclusive serial-port ownership.
  A two-channel adapter will have separate ports; identify them before use.
- Review any supplied model/protocol/register documentation first. RS485 does
  not imply Modbus. Listen for unsolicited output before sending requests.
- If Modbus is indicated, use a standard RTU library and bounded function 03/04
  reads. Start with documented settings; absent documentation, first try address
  1 at 9600 8N1, then a small set of common settings. Never use broadcast reads,
  configuration writes, calibration commands, or an unrestricted address sweep.
- Record baud/parity, address, function, zero-based register, raw payload,
  CRC-valid responses/exceptions, and response timing. Decode only documented
  units, scaling, signedness, missing-value codes, and word ordering; do not
  infer nine sensor channels from a successful register block alone.
- Close the probe port after the test. Re-enable the display gateway only after
  restoring the appropriate display wiring, not while an unknown sensor remains
  isolated on the same channel.

## Adapter Roadmap

Keep the N2K instrument network separate from the Semantic Boat device network.
Dedicated CAN device PGNs/commands remain to be specified; N2K devices should
bind by NAME/address-claim identity rather than mutable source address.

The runtime separates device sessions/catalog from transport adapters. Addressed
RS485 is implemented with explicit direction, master grants, bounded reply
windows, retry replay and duplicate configured-address rejection. Physical
addresses must still be set uniquely; automatic identity/discovery and CAN
adapters remain future work. Multiple devices on a legacy serial bus are rejected.

This local monorepo plugin reuses `src/tools/disp-sim/src/protocol.js` and the display
JSON schema. Its templates live in `configs/`. It is not yet a standalone npm
distribution; move shared codecs/schema to published packages before publishing.

## Bundled Configurations

- `environment.json`: apparent/true wind, air/sea temperature, air pressure.
- `navigation.json`: SOG/STW, true course/heading, vessel state, depth references.
- `engine.json`: `propulsion.port` RPM, coolant, oil pressure, alternator, hours.
- `electrics.json`: 12 V house/starter batteries and port alternator voltage.
  Battery paths use the conventional `electrical.batteries.house` and `starter`
  identifiers; this server has no battery-monitor data yet, so those values are
  unavailable until a provider publishes them. Edit identifiers to match future
  monitors. No Raspberry Pi or Sailor HAT readings are included in this config.
- `server.json`: live `environment.rpi` CPU temperature/utilisation, RAM/disk
  utilisation, uptime, and 1/5/15-minute load averages.
- `weather-station.json`: all 15 SN-*-FSXCS-N01 measurements under
  `environment.weather`, plus availability and last successful read. Five pages
  cover wind, air, particulates, light/rain, and station status. Particulates use
  `ug/m3` with whole-number values; rainfall uses metres with four decimal places. Station
  direction and compass readings are not labeled as vessel heading or true wind.

Catalog changes do not modify the app already installed on a display. Use Apps
on the device or the management page to explicitly install a configuration.

## Checks

```sh
cd src/signalk/plugin/semantic-boat-devices
npm install
npm test
```

Tests cover preferred-source binding, replacement/cleanup, malformed snapshots,
catalog limits/path safety, transfer CRC/chunk offsets, serial pacing/lifecycle,
plugin routes, and unsafe bus configuration rejection.