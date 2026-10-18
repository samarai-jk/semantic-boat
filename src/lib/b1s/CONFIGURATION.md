# B1S configuration over CAN (design draft)

**Status:** draft for review, 2026-10-18. Part of [B1S](README.md); nothing
here is implemented yet except fast-packet reception in `src/lib/n2k`.

Goal: configure, calibrate, and diagnose B1S devices from the Raspberry Pi,
so that a device in the field never needs the ST-Link. The same mechanism
backs up every device's configuration on the Pi and restores it onto a
replacement device.

## Overview

```text
 browser: Semantic Boat Devices page ──HTTP──▶ B1S plugin (Signal K)
                                               │  parameters, actions, backups,
                                               │  console sessions
                                               ▼
                                 B1S provider (can1, own events)
                                               │  Service 126720 (addressed fast packet)
                                               │  Command 61184 (addressed single frame)
                                               ▼
                              device: b1s::ParameterService + device parameters
                                      (slstm32 measurement registry, channel
                                       configuration, device settings in EEPROM)
```

- **Device firmware** (`src/lib/b1s`): a `ParameterService` answers
  parameter reads and writes and runs actions. What a device offers is a
  table in its composition root, built from generic pieces (channel
  configuration, measurement calibration and thresholds, device role).
- **B1S plugin** (`src/signalk/plugin/b1s`): speaks the protocol, keeps a
  backup of every device's parameters, and offers an HTTP API.
- **Semantic Boat Devices plugin**: the user interface. Its page gets a
  B1S view next to the display devices: devices, channels with live values,
  editable parameters, calibration actions, backup and restore, and (if
  decided) a console. It calls the B1S plugin's API; protocol code stays in
  one place.

## Parameters

A **parameter** is a named, typed value of a device or of one of its
channels. Parameters are defined in the registry (`parameters` section), not
in the device: the device stores only which parameters it has and how to get
and set them, so names, descriptions, and units cost no device flash. The
tool reads everything else from the registry.

| Field | Meaning |
| --- | --- |
| `id` | uint16, never reused |
| `id` name | lowerCamelCase, e.g. `channelLocation` |
| scope | `device` or `channel` (then addressed with a channel number) |
| type | `uint8`, `uint16`, `int32`, `bool`, `enum`, `string` (max 32 ASCII) |
| unit | for `int32` values of a channel: `quantity` means the channel quantity's raw unit (SI x scale, as in the Measurement message); otherwise an SI unit |
| access | `rw` (stored, part of backups), `ro` (state, diagnostics) |
| restart | the new value takes effect after a restart |

First set (ids are proposals):

| Id | Name | Scope | Type | Access | Content |
| --- | --- | --- | --- | --- | --- |
| 1 | `deviceRole` | device | string | rw | Device role, e.g. `fridgeSensor`; also installation description 1 (126998) |
| 2 | `deviceInstance` | device | uint8 | rw, restart | NAME device instance |
| 3 | `preferredAddress` | device | uint8 | rw | Start address of the claim |
| 16 | `channelLocation` | channel | string | rw | Signal K path fragment; empty = unassigned |
| 17 | `channelLabel` | channel | string | rw | Label for people |
| 18 | `channelEncoding` | channel | enum `automatic`, `standard`, `b1s`, `off` | rw | Encoding switch |
| 19 | `channelStandardInstance` | channel | uint8 | rw | Instance of the standard PGN |
| 32 | `calibrationOffset` | channel | int32, quantity | rw | value = raw x gain + offset |
| 33 | `calibrationGain` | channel | int32, ppm | rw | |
| 40..43 | `alarmThreshold0..3` | channel | int32, quantity | rw | Threshold of alarm rule n (rate rules: quantity per minute) |
| 64 | `uptime` | device | int32, s | ro | |
| 65 | `restarts` | device | ... | ro | Health record counters, supply minimum, ... |

Values on the wire use B1S units (SI x quantity scale); the device converts
to its internal units (inverse of `b1s::toRaw`).

**Where values live on the device:**

- Channel location, label, encoding, standard instance, device role and
  instance: a new B1S configuration record in EEPROM, keyed by B1S channel
  number. The compiled-in `ChannelConfig` stays the default.
- Calibration and alarm thresholds: unchanged, the slstm32
  `MeasurementRegistry` settings keyed by the *sensor's* identity (they
  belong to the physical sensor, not the channel).

## Actions

One-shot operations, as Command messages (61184, registry `commands`):

| Command | Argument | Effect |
| --- | --- | --- |
| `identify` | seconds | Blink and beep (exists) |
| `calibrate` | channel, reference (int32, quantity) | Offset calibration against a reference, like `sensor <name> cal` |
| `calibrateSecondPoint` | channel, reference | Gain and offset from two points (`cal2`) |
| `resetChannel` | channel | Calibration and thresholds back to defaults |
| `acknowledgeAlarms` | — | Like any button or `ack` |
| `restart` | — | Software restart |
| `factoryReset` | — | All stored configuration back to defaults (asks for confirmation in the tool) |

## Service message (126720)

Addressed fast packet: proprietary header, service code, sequence number
(pairs a reply with its request), then the service's fields. Requests and
replies share the service code; bit 7 marks a reply.

| Code | Service | Request | Reply |
| --- | --- | --- | --- |
| 1 | `parameterList` | first index | up to 30 parameter ids the device has, with scope, plus "more" |
| 2 | `parameterRead` | id, channel | status, type, value |
| 3 | `parameterWrite` | id, channel, type, value | status, value as stored |
| 4 | `console` | command line | *(if decided)* the lines the command logged, in several replies |

Status codes: 0 done, 1 unknown parameter, 2 unknown channel, 3 invalid
value, 4 read-only, 5 busy, 6 not permitted.

A device answers one request at a time; the tool waits for each reply
(timeout 1 s, three tries). Writes are stored at once; a reply means the
value is in EEPROM.

## Pi side

- **B1S plugin API:** `GET /devices` (exists),
  `GET /devices/:id/parameters` (all values, read live),
  `PUT /devices/:id/parameters/:name[/:channel]`,
  `POST /devices/:id/actions/:command`, `GET|POST /devices/:id/backup`,
  `POST /devices/:id/restore[?from=<device ID>]`, and (if decided)
  `POST /devices/:id/console`.
- **Backups:** after every change and once a day, the plugin stores each
  device's `rw` parameters as JSON in its data directory, by device ID and
  product, with the time.
- **Replacement:** a device of the same product without a role appears
  while a configured one has been missing for a while: the page offers
  "restore the configuration of *X* onto this device". Calibration is not
  copied (it belongs to the old sensors).
- After a location change, Signal K paths change at once (the plugin
  re-reads the Channel Descriptions the device sends after each change).

## Memory

Estimate for LT1 (product build): parameter service and actions 2..3 KiB of
flash, about 100 bytes of RAM besides the 96-byte receive buffer (already
in place); the console tunnel about 0.5 KiB more. The parameter table is
`const` (flash).

## Decisions needed

1. **Console over CAN.** Tunnel the existing text console (all its commands
   and their answers) through the service message. Cheap and powerful for
   diagnosis, and the devices page gets a terminal. AGENTS.md treats every
   exposure of the console as a product decision; development commands
   (`trap`, `i2c write`) stay excluded from product builds anyway.
2. **Write protection.** Anyone who can send on B1S can configure devices.
   Proposal for now: none (B1S is wired inside the boat); later possibly a
   per-device lock parameter that only a button press on the device opens.
3. **User interface in the devices plugin**, with the protocol in the B1S
   plugin (as above), or one plugin for both.

## Implementation steps

1. Registry: `parameters` section, new commands, service codes; generator
   and can-bench support.
2. Firmware: `b1s::ParameterService` (list, read, write), channel
   configuration record, measurement parameters and actions; LT1 wiring;
   host tests; size report.
3. B1S plugin: service client, API, backups.
4. Devices plugin: B1S view.
5. Bench test end to end: set LT1's air channel location to `inside`, watch
   the Signal K path change; calibrate the air temperature from the page;
   restore a backup.
