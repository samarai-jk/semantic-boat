# N2K Simulator

A development-only sailboat instrument simulator for Semantic Boat hardware
and Signal K data-processing tests. Requires Node.js 20 or newer.

**Never enable this on a live navigation server.** Direct mode writes to
`vessels.self`; the N2K feed is synthetic vessel data. A distinct source label
does not isolate simulated values from consumers of the self-vessel paths.
Use a separate development Signal K instance.

## Implemented

- Marina, anchor, and underway modes with deterministic variation.
- Geodesic travel toward a destination, acceleration, and current drift.
- Engine start/stop and RPM; wind, waves, depth, and temperature controls.
- Validated live controls, pause/resume, and trip reset.
- Bounded smooth fluctuations with irregular seeded targets and per-reading transmission switches.
- Encoded NMEA 2000 PGNs using canboat, not handwritten byte layouts.
- Local TCP feed plus a Signal K server plugin entry point.
- Responsive browser helm with a live chart, route editing, and instrument controls.
- Environment, Engine, and Nav & derived control tabs; named browser-local presets.
- Real Signal K 2.33 integration verified for native N2K decoding and direct deltas.

Server-shared scenarios, fault injection, configurable sensor rates, and physical
CAN transport are **not implemented yet**. See
[PLAN.md](PLAN.md) for the staged design.

## Run without Signal K

From this directory:

```sh
npm ci
npm test
npm start
```

This starts an Actisense-text N2K feed at `127.0.0.1:10112` and a JSON control
API at `http://127.0.0.1:10113`. Both bind to loopback only. The API is not a
separate data source: the same runtime serves the helm at
`http://127.0.0.1:10113/ui/`. Stop with `Ctrl+C`.

```sh
npm start -- --mode underway --port 10112 --api-port 10113
curl http://127.0.0.1:10113/state
curl -X POST http://127.0.0.1:10113/controls \
  -H 'Content-Type: application/json' \
  -d '{"mode":"underway","engineRunning":true,"engineRpm":2200,"windSpeed":10,"waveHeight":1.5}'
```

Use `/pause`, `/resume`, and `/reset` with POST and a JSON body (`{}`). Reset
returns to the departure position and resets elapsed trip time/engine hours,
retaining the current controls. Changing departure coordinates also resets
the trip; changing the destination changes the active target without reset.
Cross-origin browser control requests are rejected in standalone and plugin modes.
The helm shows speeds in knots and angles in degrees. Numeric fields commit on
change; sliders apply continuously with a short debounce. Apply route submits
both departure and destination, so it resets the trip. Chart tiles and the
preferred fonts require internet access; controls and local assets do not.
Map tile images send an origin-only referrer to comply with OpenStreetMap's
tile policy, even when Signal K sets a global `no-referrer` policy. If cached
tiles show "Access blocked", hard-refresh the dashboard after updating the plugin.

## Tabs and Presets

The chart stays on the left, with **Environment**, **Engine**, and
**Nav & derived** tabs on the right (stacked below the chart on mobile).
Environment wind-speed controls cover 0-60 knots. Engine includes coolant
temperature; navigation contains the remaining derived readings.

Use the save icon beside **Preset**, enter a name, and save. Selecting a
name immediately restores that preset. Saving with an existing name replaces
it after the dialog displays a replacement notice; the delete icon removes
the selected preset after confirmation.

Presets capture every model control, departure/destination, current position,
fluctuation bounds, all transmission switches, and pause state. Pending slider
edits are committed before saving; apply any draft route first. Loading clears
extra fluctuation ranges, resets trip time/engine hours, relocates to the saved
position, and restores the saved paused/running state. It does not resume the
precise historical physics state or save elapsed time, counters, UI tabs, chart
zoom, Signal K source priorities, or plugin transport configuration.

Presets persist in this browser's local storage for this server/dashboard,
including across server restarts. They are not shared between browsers or
devices; clearing site data removes them. Saving does not change Signal K
configuration, and presets never load automatically on opening the dashboard.

## Fluctuations and Sensor Switches

Each adjustable physical input has **one two-handle range slider** and linked
**Min/Max** fields. The left handle sets the minimum and the right sets the
maximum. Both handles and numeric fields edit the same bounds; there is no
separate setpoint slider or Fluctuate switch. Equal bounds hold a **Fixed**
value; different bounds show **Variable** and the model smoothly moves
toward changing targets inside that range, choosing a different target and
transition duration each time. It does not sweep repeatedly from minimum to
maximum or inject independent random noise on each transmitted sample.
The sequence is seeded per input, reproducible after reset, and unaffected by
dashboard polling. New bounds take effect immediately; changing bounds may
clamp a value that is now outside the permitted interval.

These are representative test timescales, not a calibrated vessel/weather model:

| Input | Target transition time |
| --- | --- |
| Engine RPM / oil pressure | 3-15 seconds |
| Alternator voltage | 5-30 seconds |
| Wind speed / direction | 8-35 / 15-60 seconds |
| Sailing speed | 20-90 seconds, plus vessel acceleration |
| Wave height / period | 30-120 / 60-180 seconds |
| Depth / anchor radius | 10-60 / 20-90 seconds |
| Coolant temperature | 30-180 seconds |
| Current speed / direction | 120-600 / 180-900 seconds |
| Air / water temperature | 120-600 / 300-1200 seconds |
| Atmospheric pressure | 300-1200 seconds |

Waves retain their selected oscillation period with a slowly changing amplitude;
period edits preserve phase. Heading, position, COG/SOG, apparent wind, and
attitude are derived from the route, speed, current, wind and sea state rather
than assigned independent ranges that could contradict the vessel's movement.
Engine hours accumulate while the engine runs. Engine-off RPM and oil pressure
remain zero, irrespective of configured running ranges.

Every emitted reading has its transmission switch **beside its live value**,
including separate roll/pitch/yaw and fields that share a PGN. Derived readings
without adjustable ranges retain their live value and switch. **Enable all** and
**Disable all** change transmission, not the physical model. Disabled readings
are omitted from direct deltas and encoded as unavailable in shared N2K PGNs;
entirely disabled PGNs are not sent. Existing values in Signal K are not erased:
they remain last-known/stale until the server or consumer expires them. The
dashboard continues to show the physical model even if its sensors are disabled.

Live bounds and switches are session controls, retained by trip reset but not
persisted across plugin/server restart. API examples use the control-input units
in the table below (m/s, Celsius, pascals), not the dashboard's knots/bar/hPa:

```json
{
   "fluctuations": { "windSpeed": { "min": 4, "max": 10 } },
   "outputs": { "environment.water.temperature": false }
}
```

Send `null` for an input's fluctuation bounds to return to its fixed setpoint.
`GET /parameters` lists supported inputs/timescales and transmission switches.

## Movement and Dragging

Drag the vessel marker to relocate its current position, or the flag marker to
change the destination. Both apply immediately without resetting underway trip
time. In anchor mode, dragging the vessel relocates the anchorage and resets
the trip. The existing departure/destination inputs and chart picker tools also
remain available. Apply route still resets the trip because it submits departure.

Underway sailing uses the sailing-speed setpoint or its active bounds. With the
engine on, sailing-speed controls are disabled and speed through water follows
`RPM / 600` m/s, with an eight-second acceleration response. For example,
2400 RPM targets 4 m/s (about 7.8 knots). This is a simple test calibration, not
an engine/propeller performance model. Position advances using that same speed
and the current vector; SOG can therefore differ from speed through water.

Heading follows the bearing to the destination underway; course over ground
follows the actual motion including current. In marina mode, edit **True heading**
directly in the top readings. The `marinaHeading` control is in degrees true
(0-360); position stays fixed and heading/course remain at that setting,
independent of wind, waves, and engine state. Returning to marina restores it.

At anchor, departure coordinates represent the anchor. The vessel faces into
the wind and settles downwind of the anchor with a gentle, deterministic swing.
The anchor-only **Chain paid out** field sets chain length (default 40 metres).
The maximum horizontal swing radius is `sqrt(max(0, chainLength² - depth²))`:
50 metres of chain in 30 metres of water allows a 40-metre radius. The chart
circle and radius reading use this derived limit. Stronger wind stretches the
rode farther within it, while calm wind lets it settle back toward the anchor. Wind direction
changes turn the boat over an eight-second response and move its position over
a twenty-second response rather than teleporting it. Speed/course over ground
come from that actual movement, so course need not equal bow heading while
swinging. The chart shows the maximum swing circle, anchor-to-vessel line, and
current anchor distance. This is a representative wind-only test model, not a
calibrated mooring model: depth below the transducer approximates bow-to-bottom
height; bow height, catenary, current and anchor dragging are not modelled.
Chain shorter than depth displays a warning and clamps the horizontal radius
to zero; this does not simulate a valid anchorage. Shortening chain or increasing
depth can immediately clamp the position to the new limit. The existing
`anchorRadius` API control remains an optional additional cap (default 200m).

## Signal K Plugin

Install the local package into your **separate development** Signal K user
directory. Substitute its actual config directory below, not your live server's:

```sh
cd /path/to/development-signalk-config
npm install /home/skipper/semantic-boat/src/signalk/plugin/n2k-simulator
```

Restart Signal K, then enable **Semantic Boat N2K Simulator** in its plugin
configuration. This package is private/local and is not published to the
Signal K Appstore. Two output modes are available:

1. `n2k` (default): listen on localhost TCP port 10112, sending timestamped
   Actisense CSV lines. Add the native provider below to exercise the server's
   normal canboat decode pipeline. The simulator listens; Signal K connects.
2. `signalk`: publish SI-unit deltas using `app.handleMessage` and source
   `semantic-boat.n2k-simulator`. This tests consumers and alarms but bypasses
   NMEA 2000 decoding. The TCP feed remains available for inspection.

**Do not connect the TCP input and enable direct Signal K injection together.**
That would inject duplicate competing values. Likewise, do not run standalone
and plugin mode on the same TCP port.

The plugin registers `GET /state`, `GET /defaults`, and POST `/controls`,
`/pause`, `/resume`, `/reset` under the server's plugin router (plugin ID
`semantic-boat-n2k-simulator`). Open the helm at
`http://localhost:3000/plugins/semantic-boat-n2k-simulator/ui/`, substituting
your server port. It also appears in Signal K's **Webapps** list while enabled;
that launcher opens the same authenticated dashboard. Restart the server once
after installation or metadata updates to refresh discovery.
Routes inherit Signal K's plugin authentication/authorization
policy (admin access by default). Do not expose the simulator to an untrusted network.

### Native N2K connection

This is a tested `pipedProviders` entry for a stopped development server's
settings. Append it to the existing array; do not replace other settings:

```json
{
   "id": "n2k-simulator",
   "enabled": true,
   "pipeElements": [
      { "type": "providers/tcp", "options": { "host": "127.0.0.1", "port": 10112 } },
      { "type": "providers/liner", "options": {} },
      { "type": "providers/canboatjs", "options": {} },
      { "type": "providers/n2k-signalk", "options": { "useCanName": false } }
   ]
}
```

Disable CAN-name identity because the simulator does not yet emit address claims.
In `n2k` mode, enabling the simulator without this input only opens its TCP feed;
it does not populate Signal K values. Direct `signalk` mode needs no input.
Do not choose a serial/USB Actisense binary input: this feed is CSV text.
If Signal K is in a container, localhost must refer to the same network namespace;
the simulator deliberately does not listen on external interfaces.

### How data reaches Signal K

```text
UI -> validated controls -> boat model -> instrument scheduler
    -> canboat PGN encoder -> localhost TCP CSV
    -> Signal K TCP + line splitter -> canboat decoder
    -> native N2K-to-Signal K mapper -> app.handleMessage
    -> self-vessel data tree -> REST/WebSocket subscribers
```

The UI edits the model; it does not post readings to Signal K. The native
provider continuously consumes the stream and maps PGNs into Signal K paths.
For example, PGN 127488 at 2400 RPM becomes
`propulsion.port.revolutions = 40` (revolutions/second), with source
`n2k-simulator.200`. Position becomes `navigation.position`, water temperature
becomes `environment.water.temperature` in Kelvin, and so on.

In `signalk` output mode the plugin instead calls `app.handleMessage` with
already-built SI-unit deltas. No provider configuration is needed. The source
is `semantic-boat.n2k-simulator` and its engine path is
`propulsion.main.revolutions`, rather than the native mapper's `propulsion.port`.
This simpler mode exercises consumers, not the N2K encoder/decoder path.

Once ingested, downstream plugins and clients see ordinary Signal K values.
Delivering them to Semantic Boat hardware still requires its normal subscribed
data/gateway path; this simulator does not itself send physical CAN or serial data.

### Verification

`npm test` covers the model, PGNs, rates, runtime, and lifecycle. The opt-in
real-server check creates a temporary config, installs a local plugin symlink,
loads the plugin, verifies UI assets and cross-origin rejection, and asserts
REST values/sources through both ingestion modes:

```sh
SIGNALK_SERVER_MODULE=/path/to/node_modules/signalk-server node scripts/check-signalk.js
```

Verified with Signal K 2.27.0 and 2.33.0 on Node 22.17.0 (those servers recommend Node 24).
The test server briefly listens on a random HTTP port on all interfaces, as
Signal K normally does; run it on a trusted development host. It does not read
or modify your normal Signal K config. Security-enabled login flows and actual
hardware delivery remain environment-specific checks. Browser controls, map
imagery, screenshots, and overflow were checked at 1440px desktop and 390px mobile.

## Controls and units

Controls accept only known fields and finite values; invalid patches are
rejected without partially applying changes.

| Control | Units / meaning |
| --- | --- |
| `mode` | `marina`, `anchor`, `underway` |
| `departureLatitude`, `departureLongitude` | Degrees; changing either resets the trip |
| `destinationLatitude`, `destinationLongitude` | Degrees |
| `sailingSpeed` | m/s; prescribed test speed, not calculated from polars |
| `engineRunning`, `engineRpm` | Boolean and RPM; simplified motoring speed when underway |
| `windSpeed`, `windDirection` | m/s and degrees true, direction **from** |
| `currentSpeed`, `currentDirection` | m/s and degrees true, direction **toward** |
| `waveHeight`, `wavePeriod` | Metres and seconds |
| `airTemperature`, `waterTemperature`, `coolantTemperature` | Celsius controls; Kelvin output |
| `oilPressure`, `alternatorVoltage`, `atmosphericPressure` | Pascals, volts, pascals; dashboard shows bar, volts, hPa |
| `depth` | Metres below transducer |
| `chainLength` | Metres of chain paid out (0-200) |
| `anchorRadius` | Optional horizontal radius cap in metres (default 200) |

Signal K angles are radians, speeds m/s, RPM is converted to revolutions per
second, temperatures Kelvin, and pressure pascals. Canboat handles N2K wire
resolution. Engine time starts at zero per trip.

## Instrument schedule

These are representative test rates, not a claim of complete NMEA device
certification. Slow clients are disconnected rather than allowed unbounded
buffer growth. Scheduler delays skip missed samples instead of emitting bursts.

| PGN | Instrument | Rate |
| --- | --- | --- |
| 127250 | True heading | 10 Hz |
| 127257 | Pitch/roll | 10 Hz |
| 129025 | Position | 5 Hz |
| 129026 | COG/SOG | 5 Hz |
| 128259 | Speed through water | 2 Hz |
| 128267 | Depth | 1 Hz |
| 130306 | Apparent wind / true ground-referenced wind | 10 Hz / 2 Hz |
| 127488 | Engine RPM | 10 Hz |
| 127489 | Engine dynamic parameters | 2 Hz |
| 130310 | Air/water temperatures and pressure | 1 Hz |

The TCP stream contains whole PGN payloads, including fast-packet PGN 127489;
it is not raw CAN frames, and does not transmit onto physical NMEA 2000. Address
claims, device discovery, ISO requests, and GNSS fix metadata are future work.

## Model limits

This is an instrument test rig, not navigation or hydrodynamics software.
Marina position is fixed. Anchor mode follows a bounded synthetic swing about
departure with stationary SOG in this first version. Underway steers directly
toward the destination and adds current; it has no coastline/depth chart or
collision avoidance. Water depth and temperatures are prescribed, not geographic.
Sea state produces wave-period attitude with an irregular amplitude envelope,
not a spectral ocean model. Depth varies only within explicitly enabled bounds.
Engine RPM and temperature controls can be set independently to exercise
alarms; there is no thermal warm-up model yet. No simulated alarm notifications
are injected: consumers must derive alarms from the readings.