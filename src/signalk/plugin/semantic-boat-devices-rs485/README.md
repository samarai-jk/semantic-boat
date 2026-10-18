# Semantic Boat Devices RS485

Signal K plugin for the legacy Semantic Boat displays (remote-a) on RS485 or
another serial line, speaking the semantic-link protocol. It is a
[transport](../semantic-boat-devices/README.md#transports) of
[Semantic Boat Devices](../semantic-boat-devices/README.md): the hub lists the
displays on its Devices page, keeps the display designer and configuration
catalog, and installs configurations through this plugin. Without the hub the
displays keep their live values and installed configurations; installs and
the configuration list on the display (**Apps**) need the hub.

Each display has an independent session with preferred-source live data. The
plugin supports point-to-point links and a shared, master-polled RS485 bus with
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

```sh
(cd /path/to/semantic-boat/src/signalk/plugin/semantic-boat-devices-rs485 && npm install)
ln -s /path/to/semantic-boat/src/signalk/plugin/semantic-boat-devices-rs485 ~/.signalk/node_modules/@semantic-boat/devices-rs485
```

Add `"@semantic-boat/devices-rs485": "file:../semantic-boat/src/signalk/plugin/semantic-boat-devices-rs485"`
to the dependencies in `~/.signalk/package.json`, enable **Semantic Boat
Devices RS485**, configure buses and displays, and restart the server.

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
  timeout, unexpected-reply and decode counters are available in the RS485 plugin's `/state`.
- Transfers use 32-byte chunks and CRC-32. `sent-awaiting-device-restart` means
  bytes were sent, not that installation succeeded: v1 has no install ACK.
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

## Checks

```sh
cd src/signalk/plugin/semantic-boat-devices-rs485
npm install
npm test
```

Tests cover preferred-source binding, replacement and cleanup, malformed
snapshots, serial pacing and lifecycle, the addressed bus, unsafe bus
configuration, and registration with the hub. The addressed-bus test uses the
hub's catalog (`../semantic-boat-devices/src/catalog.js`).
