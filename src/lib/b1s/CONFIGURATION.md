# B1S configuration over CAN (design draft)

**Status:** 2026-10-19. Part of [B1S](README.md). Implemented: the device
side (`b1s::ServiceServer`, `b1s::ChannelSettings`, on LT1) and the
command-line client [`src/tools/can-bench/b1s_tool.py`](../../tools/can-bench/b1s_tool.py).
Planned: the Signal K side (backups, replacement, the devices page).

Decisions (2026-10-18): the console is reachable over CAN in all builds
(development commands stay out of product builds anyway); no write
protection for now (anyone on B1S may configure); the B1S protocol and the
user interface live in one Signal K plugin, Semantic Boat Devices.

Goal: configure, calibrate, and diagnose B1S devices from the Raspberry Pi,
so that a device in the field never needs the ST-Link. The same mechanism
backs up every device's configuration on the Pi and restores it onto a
replacement device.

## Overview

```text
 browser: Devices page of Semantic Boat Devices
                    ──HTTP──▶ Semantic Boat Devices plugin (Signal K)
                                               │  parameters, commands, backups,
                                               │  console sessions
                                               ▼
                                 B1S provider (can1, own events)
                                               │  Service 126720 (addressed fast packet)
                                               │  Command 61184 (addressed single frame)
                                               ▼
                              device: b1s::ServiceServer + device parameters
                                      (slstm32 measurement registry, channel
                                       configuration, device settings in EEPROM)
```

- **Device firmware** (`src/lib/b1s`): a `ServiceServer` answers
  parameter reads and writes and runs commands. What a device offers is a
  table in its composition root, built from generic pieces (channel
  configuration, measurement calibration and thresholds, device role).
- **Semantic Boat Devices plugin**
  ([B1S Devices](../../signalk/plugin/semantic-boat-devices/README.md#b1s-devices)):
  B1S measurements to Signal K, the configuration client, backups, and the
  B1S devices on its Devices page: channels with live values, editable
  parameters, calibration, backup and restore, and a console.

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
| type | `uint8`, `uint16`, `int32`, `bool`, `enum`, `string` (max 32 ASCII), `record` (fields, see below) |
| unit | for `int32` values of a channel: `quantity` means the channel quantity's raw unit (SI x scale, as in the Measurement message); otherwise an SI unit |
| access | `rw` (stored, part of backups), `ro` (state, diagnostics) |
| restart | the new value takes effect after a restart |

The parameters are in the registry; the first set:

| Id | Name | Scope | Type | Access | Content |
| --- | --- | --- | --- | --- | --- |
| 1 | `deviceRole` | device | string | rw | Device role, e.g. `fridgeSensor` |
| 3 | `preferredAddress` | device | uint8 | rw | Start address of the claim (device-provided) |
| 16 | `channelLocation` | channel | string | rw | Signal K path fragment (letters, digits, dots); empty = unassigned |
| 17 | `channelLabel` | channel | string | rw | Label for people |
| 18 | `channelEncoding` | channel | enum `automatic`, `standard`, `b1s`, `off` | rw | Encoding switch |
| 19 | `channelStandardInstance` | channel | uint8 | rw | Instance of the standard PGN; 255 = firmware default |
| 20 | `channelStandardSource` | channel | uint8 | rw | Source field of the standard PGN (registry `standard_sources`); 255 = firmware default |
| 21 | `channelOutput` | channel | record `output` | rw | Value function and window, send period, raw value period |
| 32 | `calibrationOffset` | channel | int32, quantityDelta | rw | value = raw x gain + offset |
| 33 | `calibrationGain` | channel | int32, ppm | rw | 500000..2000000 |
| 40 | `faultAlarm` | channel | record `faultAlarm` | rw | Alarm when the channel has no valid value: off .. emergency, delay, latching |
| 41..44 | `alarm0..3` | channel | record `alarm` | rw | Alarm rule n; function `none` = unused |
| 45 | `alarmState` | channel | uint32 | ro | Bit 2n active, bit 2n+1 acknowledged; n = 0..3 rules, 4 fault alarm |
| 64 | `uptime` | device | uint32, s | ro | |
| 65 | `windowPool` | device | record `windowPool` | ro | Windows of the device: total, free, buckets per window |

`quantityDelta` values have the quantity's scale but no zero offset: a
calibration offset of 504 is 0.504 K, not -272.6 °C. A channel parameter
the channel does not have (calibration of a channel without a measurement)
reads as "unknown parameter".

### Records

A record parameter is a fixed sequence of fields, little-endian, no gaps,
laid out in the registry (`records`); the tool and the plugin decode them
from there. A record is written whole, so a rule never exists half changed.

**`output`** (8 bytes): how the channel's value is derived and how often it
is sent.

| Field | Type | Meaning |
| --- | --- | --- |
| `function` | enum `none`, `last`, `average`, `minimum`, `maximum` | Value sent (`none` = `last`) |
| `flags` | bit 0 `windowSamples` | The window counts samples, not seconds |
| `window` | uint16 | Window length for average, minimum, maximum |
| `period` | uint16, 0.1 s | How often the value is sent; 0 = firmware default |
| `rawPeriod` | uint16, 0.1 s | How often the raw value is sent (Measurement Raw); 0 = never |

**`faultAlarm`** (4 bytes): `severity` (enum `off`, `warning`, `alarm`,
`emergency`), `flags` (bit 0 `latching`), `delay` (uint16, s: how long the
channel must lack a valid value).

**`alarm`** (20 bytes): one rule, read as a sentence: "*severity* when the
*function* (over *window*) is *comparison* *threshold* (and *upper*) for
*delay* seconds; clears *hysteresis* back".

| Field | Type | Meaning |
| --- | --- | --- |
| `function` | enum `none`, `last`, `average`, `minimum`, `maximum`, `change`, `rate` | The value the rule looks at; `none` = unused slot |
| `comparison` | enum `above`, `atLeast`, `below`, `atMost`, `inside`, `outside` | `inside`/`outside` use `threshold`..`upper` |
| `severity` | enum `warning`, `alarm`, `emergency` | |
| `flags` | bit 0 `latching`, bit 1 `windowSamples` | Latching: active until acknowledged |
| `window` | uint16 | Window length (s or samples) for average, minimum, maximum, change |
| `delay` | uint16, s | How long the condition must hold; 0 = at the first sample |
| `threshold` | int32, alarmValue | Threshold; lower limit of a band |
| `upper` | int32, alarmValue | Upper limit of a band |
| `hysteresis` | int32, alarmDelta | How far back on the safe side the value must be to clear |

`alarmValue` is the quantity's raw unit (absolute) for `last`, `average`,
`minimum`, `maximum`, a difference for `change` (newest minus oldest part
of the window, e.g. the 3-hour pressure tendency), and a difference per
minute for `rate`. `alarmDelta` is a difference (per minute for `rate`).

**`windowPool`** (3 bytes, read-only): `windows`, `free`, `buckets`.

### Windows

Average, minimum, maximum, and change look at a sliding window. A window
has a fixed number of buckets (the firmware decides: `slstm32`
`StaticWindowPool<windows, buckets>`), so 15 seconds and 6 hours cost the
same memory; the precision is window / buckets (the value covers the
window plus at most one bucket). Windows of at most `buckets` samples are
exact. Every output or rule that needs a window takes one from the
device's pool; a write that would need more than the pool has is refused
with "no resources" and changes nothing. Time windows start at multiples of
their bucket length and values are sent at multiples of the period, so a
10 s average sent every 10 s covers each 10 s once.

Values on the wire use B1S units (SI x quantity scale); the device converts
to its internal units (inverse of `b1s::toRaw`).

**Where values live on the device:**

- Channel location, label, encoding, standard instance and source, send
  periods, device role: the B1S configuration in EEPROM
  (`b1s::ChannelSettings`), keyed by B1S channel number. The compiled-in
  `ChannelConfig` stays the default.
- Alarm rules, fault alarm, and output function: the slstm32
  `MeasurementRegistry`, per channel (they say what the channel is used
  for, so a replaced sensor keeps them), five records of 128 bytes per
  channel tagged with the channel's name. The `MeasurementConfig` stays the
  default (`resetAlarmsAndOutput`).
- Calibration: the slstm32 `MeasurementRegistry` settings keyed by the
  *sensor's* identity (it belongs to the physical sensor, not the channel).

## Actions

One-shot operations, as Command messages (61184, registry `commands`):

| Command | Channel | Argument | Effect |
| --- | --- | --- | --- |
| `identify` | 0xFF | seconds (bytes 4..5) | Fast green blink and a beep; 0 stops |
| `calibrate` | channel | reference (int32, quantity) | Offset calibration against a reference at the current reading, like `sensor <name> cal` |
| `calibrateSecondPoint` | channel | reference | Gain and offset from both points (`cal2`) |
| `resetCalibration` | channel | — | Calibration back to the defaults (offset 0, gain 1) |
| `resetAlarmsAndOutput` | channel | — | Alarm rules, fault alarm, output function, and send periods back to the firmware defaults |
| `acknowledgeAlarms` | 0xFF | — | Like any button or `ack` |
| `restart` | 0xFF | — | Software restart after the reply |

Results: 0 done, 1 unknown command, 2 unknown channel, 3 invalid argument
(implausible gain), 4 refused (no valid reading, no first point), 5 busy.

## Service message (126720)

Addressed fast packet. Request: proprietary header (bytes 0..1), service
code (2), sequence number (3, pairs the reply with its request), fields
(4..). Reply: header, service code with bit 7 set, the request's sequence
number, status (4), fields (5..).

| Code | Service | Request fields | Reply fields |
| --- | --- | --- | --- |
| 1 | `parameterList` | first index (uint8) | first index, count, more (0/1), count x parameter id (uint16); at most 32 per reply |
| 2 | `parameterRead` | id (uint16), channel (uint8, 0xFF for device parameters) | id, channel, type, value |
| 3 | `parameterWrite` | id, channel, type, value | as read: the value as stored |
| 4 | `console` | command line (ASCII, rest of the message; empty = keep alive) | log text (rest of the message), several replies |
| 8 | `logStream` | minutes (uint8): 1..60 start or extend, 0 stop, 255 only ask | remaining seconds (uint16; 0 = off) |

Values: type 0 = int32 (4 bytes), type 1 = text (NMEA 2000 STRING_LAU,
ASCII, at most 32 characters), type 2 = record (length byte, then the
record's bytes). Status: 0 done, 1 unknown parameter, 2 unknown channel,
3 invalid value, 4 read-only, 5 busy, 6 not permitted, 7 no resources (no
window left).
A reply without status 0 ends after the channel byte (read/write) or the
status (others).

A device handles one request at a time; a new request replaces one whose
reply has not been sent. The client waits for each reply (1 s, three
tries). Writes are stored at once: a reply means the value is in EEPROM.
Location, label, encoding, instance, and output changes re-send the
channel's Channel Description, so Signal K paths follow at once.

**Log stream and console:** a device sends its log (every level, debug
included) to one requester only when asked, as `console` replies of up to
200 characters:

- `logStream` starts the stream for 1..60 minutes or extends it; 0 stops it.
  A `console` request runs its line on the device console (as if typed on
  the serial port) and keeps the stream for at least 30 s, so the command's
  output arrives; it never shortens a longer stream.
- **The device stops the stream by itself** when the time is up, so a
  forgotten page or a crashed Pi never leaves it running; a restart ends it
  too. The client stops it when it shuts down.
- **Rate limit:** at most 512 bytes per second (a token bucket with
  one-chunk bursts; about 75 of the bus's ~1,700 frames per second). When
  the log grows faster, lines are dropped and the stream carries
  `[log stream: lines dropped]` instead; the bus never gets more.
- The stream reads the serial log's ring buffer with its own position (no
  extra buffer), from the moment the stream starts.

Measured on LT1 (2026-10-18): stop and automatic stop leave no further
frames; flooding the log with 20 `help` commands gave 500 bytes per second
after one initial chunk, with drop markers.

## Pi side

- **Log** of a device on its page (Semantic Boat Devices): stream for 5, 15,
  or 60 minutes, Stop, the time left, a level filter, and a console line.
- **API** of the Semantic Boat Devices plugin: see its
  [README](../../signalk/plugin/semantic-boat-devices/README.md#b1s-devices).
- **Backups:** every complete parameter read and every change stores the
  device's `rw` parameters as JSON in the plugin data directory, by device
  ID, with product and time.
- **Replacement:** Restore writes another device's backup onto a new device
  (without calibration, which belongs to the old sensors). Backups hold
  record parameters (alarm rules, output) as their fields. *Planned:* the
  page offers this by itself when a device of the same product without a
  role appears while a configured one has been missing for a while.
- After a location change, Signal K paths change at once (the plugin
  re-reads the Channel Descriptions the device sends after each change).

## Memory

Measured on LT1 (product build, 4 channels, 8 settings slots): service
server, channel settings, and console mirror +5.2 KiB of flash, +136 bytes
of RAM; fast-packet reception before that +464 bytes of flash and +144
bytes of RAM (96 of them the receive buffer); measured on the then -O3
release builds. Since release builds use -Os (2026-10-18), B1S (n2k stack,
b1s library, CAN binding) is about 9.3 KiB of LT1's 72.6 KiB. EEPROM:
`ChannelSettings::requiredBytes(slots)`, 128 bytes for the role and 384
bytes per channel slot.

Configurable alarms, outputs, and periods (2026-10-19, LT1 product build):
+5.4 KiB of flash (73,264 to 78,760 bytes: slstm32 windows, rules, and
their storage +3.0 KiB; B1S records, periods, Measurement Raw +1.8 KiB),
+2.6 KiB of RAM, of which 2.4 KiB is LT1's window pool (16 windows of 16
buckets, a device decision) and 64 bytes the send schedules. EEPROM: 640
bytes per measurement channel (`MeasurementRegistry::processingBytes()`).

## Firmware

- `b1s::ChannelSettings`: EEPROM store of the device role and, per B1S
  channel number below its slot count, location, label, encoding, and
  standard instance. The `MeasurementPublisher` applies them on top of the
  compiled-in `ChannelConfig`.
- `b1s::ServiceServer`: answers Service and Command messages. The device
  passes it every application message (`handleMessage()`), registers it as a
  service, and provides device parameters (`DeviceParameter`) and device
  commands (`CommandHandler`: identify, restart). Measurement parameters and
  the calibration commands use the slstm32 `MeasurementRegistry`
  (`calibrate()`), the console the slstm32 `CommandConsole::execute()` and the
  log mirror of `SerialLogDriver`.
- The device must receive PGN 126720 as a fast packet
  (`NodeConfig::fastReceivePgns`, buffer of at least 64 bytes; LT1: 96).

## Implementation steps

1. Done: registry (parameters, commands, services), firmware, LT1,
   `b1s_tool.py`. Verified on the bench: every parameter read, location,
   label, encoding, instance and threshold writes (stored, survive a restart,
   invalid values refused), calibrate / reset / acknowledge / identify /
   restart commands, the console with its log mirror; a location change moves
   the Signal K path at once.
2. Done: the Semantic Boat Devices plugin took over the B1S plugin;
   configuration client, API, backups, page. Verified against LT1 through
   the adapter (parameters with SI values, writes, calibration, commands,
   backup, restore, console).
3. Done (2026-10-19): configurable alarm rules (up to four per channel,
   with value function, window, comparison including bands, delay,
   hysteresis, severity, latching), configurable sensor-fault alarm, output
   function, send periods, and the raw value. Verified on LT1: rolling and
   block averages, raw values, only new samples sent, band alarm active and
   cleared, invalid band refused, settings survive a restart.
4. Planned: replacement offered automatically; daily backups.
