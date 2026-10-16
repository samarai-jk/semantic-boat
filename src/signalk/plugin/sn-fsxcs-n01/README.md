# SN-*-FSXCS-N01 Weather Station

Dedicated Signal K plugin for the particulate variant of the Shandong Saien
SN-*-FSXCS-N01 RS485 weather station, using the owner-supplied version 2.0 manual.
This is not a generic sensor or EcoWitt plugin. Register 507 is interpreted as
PM2.5, not CO2; the alternate CO2 variant is not supported by this version.

## Navigation Isolation

Every published path is under the configurable `rootPath`, defaulting to:

```text
environment.weather
```

For example, wind speed is `environment.weather.wind.speed`. Keeping this root
and the measurement suffixes stable allows a replacement weather-station plugin
to supply the same paths without changing display mappings. Alternative roots
such as `environment.weatherStations.deck` support separate stations when needed.

The default station ID is `station1`; source label is `sn-fsxcs-n01.station1`.
This ID identifies the source only and does not appear in data paths unless
explicitly included in `rootPath`. These are custom extension paths, not standard
weather paths. Units and descriptions are published as Signal K metadata.

Roots must be custom namespaces under `environment`, with dot-separated segments
starting with a letter and containing only letters and digits. Empty segments,
trailing dots, and wildcards are rejected. Standard branches `outside`, `inside`,
`water`, `depth`, `current`, `tide`, `heave`, `wind`, `time`, and `mode` are protected;
neither these roots nor their descendants may be selected. `environment` itself
and non-environment roots such as `navigation` are also rejected. The previous
`environment.weatherStations.fsxcsN01.station1` root remains valid when explicitly
configured for compatibility with earlier mappings.

The plugin never publishes to `environment.wind`, `environment.outside`,
`navigation`, or other shared instrument paths. It does not alter source
preferences, N2K providers, display bindings, or N2K output settings. Navigation
wind from the boat's N2K transducer therefore cannot be replaced or become a
fallback to this station through competing updates to the same path. Applications
must deliberately select these custom paths to use station data. Any future
exports or derived values must preserve this isolation.

## Mapping

All suffixes below follow `environment.weather.` by default, or `<rootPath>.`.
Wire addresses are decimal, zero-based; read registers 500-515 together with
function 03. PLC notation 40501 is not the address to send on the wire.

| Registers | Path suffix | Unit / conversion |
| --- | --- | --- |
| 500 | `wind.speed` | m/s, raw / 10 |
| 501 | `wind.force` | Manufacturer integer grade |
| 502 | `wind.directionSector` | 0=N, clockwise through 7=NW |
| 503 | `wind.direction` | rad, raw degrees * pi / 180 |
| 504 | `air.relativeHumidity` | ratio, raw / 1000 |
| 505 | `air.temperature` | K, signed int16 / 10 + 273.15 |
| 506 | `air.noiseLevel` | dB, raw / 10; weighting unspecified |
| 507 | `air.pm25` | kg/m3, raw micrograms/m3 * 1e-9 |
| 508 | `air.pm10` | kg/m3, raw micrograms/m3 * 1e-9 |
| 509 | `air.pressure` | Pa, raw * 100 |
| 510-511 | `light.illuminance` | lux, unsigned high word * 65536 + low word |
| 512 | `light.coarseIlluminance` | lux, raw * 100; duplicate representation |
| 513 | `rain.accumulation` | m, raw / 10000 |
| 514 | `compass.angle` | rad, raw * pi / 18000 |
| 515 | `solar.irradiance` | W/m2, raw; unit from sensor specification |
| Host status | `status.available` | Boolean; successful complete read, not sensor health certification |
| Host status | `status.lastSuccessfulRead` | ISO timestamp of host receipt |

PM2.5 and PM10 retain SI values in kg/m3, with path-specific `displayUnits`
metadata converting to micrograms/m3 (multiply by 1e9) for the Signal K Data
Browser and compatible clients. Without this conversion, fixed-decimal displays
round typical particulate concentrations to zero. This does not change global
unit preferences or other mass-density measurements. Consumers that do not use
display metadata must apply the conversion themselves.

Wind angle reference and compass correction are unverified. Compass angle is
not identified as true vessel heading. Wind aboard a moving vessel is not true
wind simply because this station includes a compass. Pressure is not corrected
to sea level. Rain counter reset, rollover, and retention across power loss are
not specified by the manual. There are no documented UV or gust registers.

Out-of-range readings become `null` using documented ranges where available.
Zero is not taken as proof that a sensor is present, absent, or faulty. The manual
does not document invalid-value sentinels or per-sensor health flags.

## Configuration And Startup

Configure under Signal K's plugin settings, using the stable USB identifier:

```json
{
  "path": "/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_BG03SD1E-if00-port0",
  "stationId": "station1",
  "rootPath": "environment.weather",
  "address": 1,
  "baudRate": 4800,
  "pollIntervalMs": 1000,
  "timeoutMs": 1000
}
```

These are host settings, not commands changing the station. Retain its verified
default 4800 baud, 8N1, address 1. The optional 2400/9600 settings are only for a
station already configured at that rate. Poll interval and timeout must exceed
200 ms; validation enforces at least 250 ms. One complete read transfers 45
bytes, approximately 94 ms at 4800 baud excluding processing and frame gaps.

Signal K persists enabled state and configuration. The plugin connects and polls
automatically on start, retrying indefinitely if the USB adapter or station is
initially absent. Reads never overlap. Failure closes the client, publishes null
measurements with `status.available=false`, and schedules another attempt.
Success restores readings. Shutdown cancels retries, waits for any pending read
to finish, closes the port, and marks measurements unavailable.

Changing `rootPath` restarts acquisition under the selected root. The previous
runtime marks its old measurements unavailable on shutdown; historical metadata
and timestamps may remain visible in Signal K. No new measurements are published
there. Consumers need remapping only when intentionally changing the root, not
when replacing the model or changing its source ID.

The plugin does not invent measurement timestamps: timestamps represent receipt
of a station response. The manual's sensor response times (up to 90 seconds for
PM) are not guaranteed update intervals or a reliable startup-ready flag. A
successful reply does not certify stabilized readings after sensor power-up.

Use a sensor channel separate from the 115200-baud display channel. Only one
plugin/master may own a serial port. While this station occupies the current
adapter, the display gateway must remain disabled. Before reconnecting displays
to that adapter, disable this plugin and move it to the new sensor adapter. Future
other-sensor plugins need separate ports or an explicitly coordinated bus owner;
this plugin must not be opened alongside another independent master on its port.

Diagnostic API (subject to Signal K's normal access controls):

```text
GET /plugins/sn-fsxcs-n01/state
```

It exposes connection availability, age, counters, errors, decoded measurements,
and the last successful raw register block. Retained raw registers are historical
diagnostics when unavailable; published measurements are null in that state.

## Calibration And Future Work

Version 0.1 is deliberately read-only. It contains no function-06 writes, no
startup reconfiguration, and no arbitrary register command endpoint.

Future model-specific controls can expose documented wind-direction reversal
(0x6000), wind-speed zeroing (0x6001, 0xAA, wait 10 s), rainfall reset (0x6002,
0x5A), and rain sensitivity (0x6003, default 0x11, smaller = more sensitive).
These actions need explicit authorization, confirmation, appropriate conditions,
and serialization with acquisition. Calibration, rainfall reset, and writes to
potential nonvolatile memory must not run automatically on every startup.
Compass recalibration requires a procedure from the vendor; none is documented
here. Address/baud write registers are also not provided in this manual.

Software calibration should retain raw values and persist correction settings.
Gust calculations should define their averaging window and validate actual
station update cadence; derived values must remain under this private namespace.
No navigation-wind fallback or automatic standard-path export is planned.

## Development

```sh
npm install
npm test
```

Install into the local Signal K environment:

```sh
npm install --prefix "$HOME/.signalk" /path/to/semantic-boat/src/signalk/plugin/sn-fsxcs-n01
```

Enable through Signal K plugin settings, then restart Signal K as required for
plugin discovery. Full boat cold-start, USB reattachment, and a long station
power-off test remain commissioning gates beyond the automated lifecycle tests.