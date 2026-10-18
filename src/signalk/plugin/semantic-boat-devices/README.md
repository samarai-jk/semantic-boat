# Semantic Boat Devices

Signal K plugin for all Semantic Boat devices, whatever connects them. It is
the hub:

- **Devices**: one list of every device of every transport, each with its
  details: B1S devices with channels, parameters, calibration, console, and
  backups; displays with their live values and configuration install.
- **Display designer**: the property/JSON editor and the shared catalog of
  `semantic-display/v1` configurations for all displays, whatever their
  transport.
- **Transports**: B1S (the Semantic Boat CAN network) is built in; other
  transports are separate plugins that register with the hub, today
  [Semantic Boat Devices RS485](../semantic-boat-devices-rs485/README.md) for
  the legacy RS485 displays. A new transport (Wi-Fi, Zigbee, ...) is a new
  plugin implementing the [transport contract](#transports); the hub, the
  designer, and the page need no change.

## Installation

Link the plugin into the Signal K configuration directory (like the other
Semantic Boat plugins) and install its dependencies:

```sh
(cd /path/to/semantic-boat/src/signalk/plugin/semantic-boat-devices && npm install)
ln -s /path/to/semantic-boat/src/signalk/plugin/semantic-boat-devices ~/.signalk/node_modules/@semantic-boat/devices
```

Add `"@semantic-boat/devices": "file:../semantic-boat/src/signalk/plugin/semantic-boat-devices"`
to the dependencies in `~/.signalk/package.json`, enable **Semantic Boat
Devices**, and restart the server. Open `/plugins/semantic-boat-devices/ui/`
(the Web Apps launcher serves the same page at `/@semantic-boat/devices/`).
Plugin configuration: only B1S (see [B1S setup](#b1s-setup)); transport
plugins have their own.

## Page

- **Devices**: the list on the left groups devices by transport, with a dot
  for online and their state; select one for its details on the right (the
  selection is kept in the address, e.g. `#b1s/1082470`).
  - A **display** shows the live values it requested (path, value, source),
    link counters, the last install, and **Install** with the catalog's
    configurations, if its transport can install.
  - A **B1S device** shows product and software, Identify, Read settings,
    Restore, and Restart, then its settings grouped as on the device:
    - **Device**: role, preferred address (after a restart), uptime, and
      the Signal K path prefix of the device (stored on the Pi, part of the
      backup; see [B1S Devices](#b1s-devices)).
    - **Channels**, one panel each: label, quantity, how it is sent (B1S
      message, standard PGN with its number and instance, or off), its live
      value with its age ("3 s ago", red when overdue; "off" when not sent),
      and its Signal K path (for standard PGN channels the path Signal K
      mapped, learned from Signal K's data). Opened, it has three tabs, each with its own clearly
      named button: **Setup** (label, location, how it is sent, standard
      instance; Save setup), **Calibration** (calibrate against a reference
      entered in °C or %: one point for the offset, a second for the gain;
      the calibration values with Save calibration values; Reset channel to
      defaults, set apart), and **Alarms** (each rule as a sentence: "Warning
      when above 40.0 °C", rise rates per minute, its state; Save
      thresholds). A Save stores only the edited fields, then reads the
      device again.
    - **Log**: the device streams its log (debug lines included) only when
      switched on there, for 5, 15, or 60 minutes, at most 512 B/s, and stops
      by itself when the time is up; Stop ends it at once. Level filter,
      console line (its output streams for 30 s).
- **Display designer**: the configuration catalog on the left, the editor on
  the right. Saving never installs; install from the display's details or
  with **Apps** on the display.

## Transports

A transport brings devices of one kind of link to the hub. The hub publishes
its interface as the Signal K property value `semanticBoatDevicesHub` (and
`null` when it stops); a transport plugin subscribes with
`app.onPropertyValues('semanticBoatDevicesHub', ...)`, takes the last value,
checks `apiVersion === 1`, and registers:

```js
const unregister = hub.registerTransport({
  id: 'rs485',                 // lowercase letters, digits, dashes; unique
  label: 'RS485',              // shown in the device list
  devices: () => [{            // called on every refresh of the page (1 s)
    id: 'remote-a-1',          // stable within the transport
    label: 'Remote-A 001',
    kind: 'display',           // 'display' gets live values and Install; others as provided
    online: true,
    status: 'online',          // short state text
    address: 'rs485-main · 001',
    detail: { /* transport-specific; displays: subscriptions, counters, lastTransfer, error */ }
  }],
  install: async (id, name) => { /* optional: send catalog configuration `name` to display `id` */ }
});
```

- `hub.catalog` is the configuration catalog: `list()`, `read(name)`,
  `validate(document)`, and the semantic-link helpers `sendList()` and
  `transfer()` used by displays speaking that protocol.
- Register again whenever the hub publishes a new value (it restarted);
  call `unregister()` when the transport stops. Without the hub a transport
  keeps running; only the page and installs are missing.
- The hub API is versioned (`apiVersion`); incompatible changes get a new
  version.
- B1S registers as the built-in transport `b1s` with kind `b1s`.

API of the hub: `GET /devices` (transports and all devices),
`POST /devices/:transport/:id/install` with `{name}`, and the catalog:
`GET /configurations`, `GET|PUT|DELETE /configurations/:name`, `GET /templates`.

## Display Designer

- The designer refreshes the catalog independently of templates and
  retries after temporary loading failures. Configurations are managed in the
  plugin's web page, not in Signal K's bus/device settings form.
- Catalog files live under the server-provided plugin data directory in
  `configurations/`; they are saved atomically and are not device-specific.
- Maximum 16 files, 47-byte filenames, 8192-byte compact JSON per configuration.
  JSON Schema validation is preliminary; the firmware compiler is authoritative.
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

## B1S Devices

[B1S](../../../lib/b1s/README.md) is the Semantic Boat CAN network, kept
separate from the NMEA 2000 instrument network. With `b1s.enabled`, the plugin
brings its devices into Signal K and configures them over CAN
([CONFIGURATION.md](../../../lib/b1s/CONFIGURATION.md)):

- **Measurements.** Canboat definitions of the B1S messages, built from the
  B1S registry (`src/lib/b1s/registry.yaml`), make Signal K decode them (also
  in the Data Fiddler and `@signalk/n2k-debug`). Each B1S measurement goes to
  `<quantity template with location>`, e.g.
  `environment.inside.refrigerator.temperature`, or, while its channel has no
  location, to `b1s.unassigned.<device ID>.<channel>.<quantity>`; SI values,
  `null` for "not available", "error", and "out of range"; metadata with the
  unit and the channel label. Standard PGNs of B1S devices are decoded by
  Signal K itself.
- **Path prefixes.** `b1s.pathPrefix` (plugin) and each device's prefix
  (Devices page, Pi-side) form a namespace for the paths of B1S messages:
  `sb.lab.environment.inside.temperature`. Standard PGN channels keep the
  path Signal K's NMEA 2000 mapping gives them; `b1s.providerId` (default
  `can1`) names the B1S connection whose mapped values the page shows.
- **Identity.** Devices are bound by the unique number of their NAME (the
  device ID), never by address; a device that moves keeps its channels.
  Unknown devices and channels are asked for their address claim, product
  information, and Channel Descriptions (at most every 10 s per device);
  global requests go out 3 s after start.
- **Devices page** for a B1S device: product, software, and
  hardware; its channels with live value, label, location, encoding, and
  Signal K path (edit and Save; Calibrate against a reference in °C or %;
  Reset calibration and thresholds); all parameters (thresholds and
  calibration in °C / K / %); Identify, Restart, Restore; and the device log
  (see Page).
- **Backups.** Every complete parameter read and every change is stored as
  `b1s-backups/<device ID>.json` in the plugin data directory. Restore writes
  a backup onto a device: its own, or another device's (a replacement; then
  without calibration, which belongs to the old sensors).
- The plugin status shows B1S devices, channels, measurements, and the age
  of the last B1S message.

API (admin login): `GET /b1s/devices`, `GET /b1s/devices/:id/parameters`
(read live, then backed up), `PUT /b1s/devices/:id/parameters/:name` with
`{channel, value}` (raw, text, or enum name) or `{channel, si}`,
`POST /b1s/devices/:id/commands/:name` with `{channel, argument | si}`,
`POST /b1s/devices/:id/console` with `{line}`,
`POST /b1s/devices/:id/log` with `{minutes}` (1..60; 0 stops),
`PUT /b1s/devices/:id/settings` with `{pathPrefix}` (Pi-side device settings),
`GET /b1s/devices/:id/console?since=<position>` (log text, next position,
seconds the stream still runs), `GET /b1s/backups[/:id]`,
`POST /b1s/devices/:id/restore` with `{from, calibration}`.

### B1S setup

1. **B1S connection.** A `canbus-canboatjs` NMEA 2000 connection on the B1S
   interface (bench Pi: `can1`, see
   [can-bench](../../../tools/can-bench/README.md#raspberry-pi-bench)) with
   its own event names, so B1S traffic stays apart from NMEA 2000 inside
   Signal K: this plugin's requests go only to B1S, other plugins' NMEA 2000
   output never reaches B1S, and this plugin sees only B1S messages.
   Preferred address 150 (the B1S range for gateways on the Pi). In
   `settings.json`:

   ```json
   {
     "id": "can1", "enabled": true,
     "pipeElements": [{
       "type": "providers/simple",
       "options": {
         "logging": false, "type": "NMEA2000",
         "subOptions": {
           "type": "canbus-canboatjs", "interface": "can1", "uniqueNumber": 600251,
           "preferredAddress": 150,
           "analyzerOutEvent": "b1s-N2KAnalyzerOut",
           "outEvent": "b1s-nmea2000out",
           "jsonOutEvent": "b1s-nmea2000JsonOut"
         }
       }
     }]
   }
   ```

   canboatjs remembers the last claimed address in `canboatjs-data.json`;
   change `lastAddress` there as well (with Signal K stopped) to move an
   existing connection.
2. **Plugin configuration:** `"b1s": {"enabled": true}` (the event names
   default to the ones above; `registryPath` overrides the repository copy of
   the registry). Restart Signal K after a registry change.

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

Tests cover the hub and the transport contract, B1S decoding (frames recorded
from LT1), paths, requests, the configuration client, catalog limits and path
safety, transfer CRC and chunk offsets, the designer, and the plugin routes.
