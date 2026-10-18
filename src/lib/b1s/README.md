# B1S protocol

B1S ("bus", b-1-s, a numeronym that pokes fun at the 2000 in NMEA 2000) is
the CAN network of Semantic Boat and the protocol its devices speak. It is
**NMEA 2000 compatible**: every B1S device is a well-behaved NMEA 2000 node
and could be connected to an NMEA 2000 network. B1S is nevertheless kept
**separate** from the boat's NMEA 2000 network, which is the navigation
backbone and must stay stable. Signal K is the only bridge between them.

The data on B1S is the sensory input of a semantic information system: every
measurement is an observation of a described signal (device, channel,
quantity, location), so it can become part of an information graph.

**Status:** draft 0.1 (2026-10-18). Implemented: NMEA 2000 network
management (`src/lib/n2k`), the standard PGNs listed in
[Standard PGNs](#standard-pgns) with the per-channel encoding switch
(compile-time), Measurement (65280), and Channel Description (130819),
verified on LT1 with can-bench, and the Signal K plugin
([`src/signalk/plugin/b1s`](../../signalk/plugin/b1s/README.md)). Everything
marked *planned* is specified here but not implemented yet.

| File | Content |
| --- | --- |
| `README.md` (this file) | The rules and message layouts |
| [`CONFIGURATION.md`](CONFIGURATION.md) | Configuration, calibration, and diagnosis over CAN (design draft) |
| [`registry.yaml`](registry.yaml) | **Every number B1S assigns**: PGNs, quantities, events, output kinds, commands, products, standard PGN categories |
| `include/b1s/registry.hpp` | Generated from the registry by `tools/generate_registry.py`; never edited by hand |
| `include/b1s/messages.hpp` | Encoders: proprietary header, Measurement, Channel Description |
| `include/b1s/quantity.hpp` | slstm32 measurement quantities -> B1S quantities and SI raw values |
| `include/b1s/measurement_publisher.hpp` | `b1s::MeasurementPublisher`: sends a device's measurement channels and their descriptions |
| `tests/b1s_tests.cpp` | Host tests of encoders and conversions (build command in the file) |

> **Rules for anyone adding to B1S, human or AI:** read
> [Adding to B1S](#adding-to-b1s) first. Never invent a PGN, quantity, event,
> command, or product code in device code; allocate it in `registry.yaml`
> following those rules.

## Contents

1. [Principles](#principles)
2. [Physical and link layer](#physical-and-link-layer)
3. [Network management](#network-management)
4. [Identity](#identity)
5. [Standard PGNs](#standard-pgns)
6. [B1S messages](#b1s-messages)
7. [Alerts](#alerts)
8. [Timing, priorities, and bus load](#timing-priorities-and-bus-load)
9. [Signal K](#signal-k)
10. [Device lifecycle](#device-lifecycle)
11. [Firmware library](#firmware-library)
12. [Adding to B1S](#adding-to-b1s)

## Principles

1. **NMEA 2000 compatible.** Identifier layout, address claim, mandatory
   network messages, fast packets, and proprietary messages follow NMEA 2000.
   Nothing on B1S may break an NMEA 2000 network if the two were connected.
2. **Separate from NMEA 2000.** B1S and the boat's NMEA 2000 network are not
   wired together. Signal K reads B1S and forwards only what is chosen, in a
   chosen form, to NMEA 2000.
3. **Standard where it fits, configurable always.** A signal uses a standard
   PGN where one fits; every signal can be switched to its B1S message or off,
   because standard PGNs from additional devices can confuse chartplotters
   (see [Standard PGNs](#standard-pgns)).
4. **Few generic messages, many signals.** Scalar values travel in one
   generic measurement message that carries a quantity code. A new kind of
   value is a new row in the registry, not a new PGN.
5. **Hardware identity is not meaning.** A device's identity comes from its
   MCU and changes when the device is replaced; the meaning of a device and of
   its channels (its *role*: "fridge", "engine room") is configuration that
   survives a replacement. Consumers bind to roles.
6. **One registry.** Every number B1S assigns is in `registry.yaml`. Numbers
   are never reused or redefined.
7. **Signal K first.** Units are SI as in Signal K, and every signal can be
   mapped to a Signal K path without per-device configuration.

## Physical and link layer

- CAN 2.0B, 250 kbit/s, sample point 87.5 %, 29-bit identifiers.
- Identifier layout, PDU1/PDU2 addressing, and priorities as NMEA 2000
  (`n2k/can_id.hpp`).
- Multi-frame messages use the NMEA 2000 fast-packet protocol, at most 223
  bytes. ISO 11783 transport protocol is not used.
- All multi-byte fields are little-endian; field conventions (resolution,
  "not available" values, strings) follow NMEA 2000.
- Cabling: a backbone terminated with 120 Ω at both ends; devices connect as
  spurs. The can-mcu connector is described in
  [can-mcu.md](../../dev/can-mcu/can-mcu.md#can-interface). The grounding
  strategy is still to be documented.

## Network management

Every device implements the NMEA 2000 network management (`src/lib/n2k`):

| PGN | Message | Device behaviour |
| --- | --- | --- |
| 60928 | ISO Address Claim | Claims its preferred address at start; the lower NAME wins; moves on and stores the new address when it loses |
| 59904 | ISO Request | Answers requests for the PGNs below and for its B1S broadcast PGNs; NACKs other addressed requests |
| 59392 | ISO Acknowledgement | NACK for unsupported addressed requests |
| 126996 | Product Information | See [Identity](#identity) |
| 126464 | PGN List | Transmit and receive lists, complete |
| 126993 | Heartbeat | Every 60 s |
| 126998 | Configuration Information | *planned*: installation description 1 = device role, 2 = free text |

Addresses:

| Range | Use |
| --- | --- |
| 0..149 | Devices. Each product has a preferred address in the registry; the claimed address is stored |
| 150..159 | Tools and gateways on the Pi (can-bench uses 153) |
| 160..251 | Devices, when the preferred range is taken |
| 254 | "Cannot claim" |
| 255 | Global |

Addresses are only a transport detail. **Nothing may depend on a device's
address**: consumers identify devices by NAME (see below), because addresses
move when devices contend.

## Identity

### NAME

| Field | B1S value |
| --- | --- |
| Unique number (21 bits) | Derived from the 96-bit MCU unique ID with `n2k::uniqueNumberFrom()`. Automatic, survives re-flashing. This is the **device ID** |
| Manufacturer code | 2046, `n2k::manufacturerCode`, for every device (not assigned by NMEA, never registered) |
| Device instance (8 bits) | 0 by default. A *meaning* ("engine 2"), set by configuration where needed, not an ID |
| Device function, class | The matching pair from the NMEA 2000 table, per product in the registry. Never a class or function the device does not perform (for example "GNSS") |
| System instance | 0 |
| Industry group | 4, marine |
| Arbitrary address capable | 1 |

Two devices collide only if their whole 64-bit NAMEs are equal. Other DIY
devices commonly use 2046 as well, but then typically with small unique
numbers; ours are hashes of the MCU ID. Among 50 B1S devices the chance of
two equal unique numbers is about 0.06 %; address contention would show it,
and a configured unique number can override the derived one.

### Product information (126996)

| Field | Value |
| --- | --- |
| Product code | From the registry (`products`) |
| Model ID | `Semantic Boat <model>` |
| Software version | Semantic version; development builds end in `-dev` (AGENTS.md) |
| Model version | Hardware, for example `can-mcu V1.0 + host-dev` |
| Serial code | MCU unique ID in hex |

### Device ID and role

- **Device ID**: the NAME unique number. Automatic from the first power-on;
  changes when the hardware is replaced.
- **Role**: what the device and each of its channels are for. The device
  role is a short identifier (installation description 1 of 126998,
  *planned*); each channel has a *location* and a *label* (see
  [Channel Description](#channel-description-130819)). Roles are
  configuration, stored on the device and backed up on the Pi.
- A **signal** is identified on the network by *(device ID, channel)* and
  described by *(quantity, location, label)*. Channel numbers are local to the
  device, so any number of identical sensors (forty temperatures on fifteen
  devices) need no coordination.

## Standard PGNs

A signal is sent as a standard PGN where one fits, so that Signal K and other
NMEA 2000 tools understand it without extra definitions. Standard PGNs used
by B1S devices are listed in the registry (`standard_pgns`) with a category.

**Encoding switch.** Every signal that has a standard PGN has a configurable
encoding:

| Mode | Sent as |
| --- | --- |
| `standard` | The standard PGN |
| `b1s` | The B1S message (measurement, output status, ...) |
| `off` | Not sent |

The default follows the category of the standard PGN:

| Category | Examples | Default | Why |
| --- | --- | --- | --- |
| `display` | Temperature, humidity, pressure, battery, engine, tanks, switch status | `standard` | Displays only show these values; extra sources are harmless |
| `navigation` | Position, time, heading, rate of turn, attitude, speed, depth, wind, rudder, magnetic variation | `b1s` | Chartplotters select a source per navigation function; an extra source can make them switch, merge, or drop the function |

Until configuration over CAN exists, a device sets each signal's mode in its
code, following these defaults.

Rules for standard PGNs:

- The data instance must be unique on the network for that PGN (and its
  source field, where the PGN has one: 130316 temperature source, 130313
  humidity source). Devices get default instances from their code and, with
  configuration over CAN, configured ones. *Planned:* a device that sees
  another NAME send the same PGN and instance raises a warning.
- Unknown or invalid values are sent as the field's "not available" value,
  never as 0 or a stale value.
- Only data the device really measures; no forwarding of other devices'
  data under its own NAME.

## B1S messages

B1S messages are NMEA 2000 proprietary messages. Every one starts with the
proprietary header:

| Bytes | Field |
| --- | --- |
| 0..1 | Manufacturer code 2046 (11 bits), reserved (2 bits, 1), industry code 4 (3 bits): `0x9FFE`, sent as `FE 9F` |

A single-frame message therefore has 6 bytes of data. Receivers check the
header and ignore proprietary messages of other manufacturers.

### PGN allocation

| PGN | Range | Message | Status |
| --- | --- | --- | --- |
| 65280 | single frame, broadcast | [Measurement](#measurement-65280) | implemented |
| 65281 | single frame, broadcast | [Output Status](#output-status-65281) | planned |
| 65282..65535 | single frame, broadcast | Reserved for B1S | |
| 130816 | fast packet, broadcast | [Measurement Wide](#measurement-wide-130816) | planned |
| 130817 | fast packet, broadcast | [Measurement Summary](#measurement-summary-130817) | planned |
| 130818 | fast packet, broadcast | [Event](#event-130818) | planned |
| 130819 | fast packet, broadcast | [Channel Description](#channel-description-130819) | implemented |
| 130820..131071 | fast packet, broadcast | Reserved for B1S | |
| 61184 | single frame, addressed | [Command](#command-61184) (multiplexed by command code) | planned |
| 126720 | fast packet, addressed | [Service](#service-126720) (multiplexed by service code) | planned |

NMEA 2000 has only one addressed single-frame and one addressed fast-packet
proprietary PGN, so the addressed messages carry a code byte after the
header.

### Values and quantities

- A **quantity** says what a value means and how it is encoded: code
  (uint8), identifier, SI unit (as Signal K), scale, encoding, and a Signal K
  path template. Quantities are semantic: `co2` is a quantity, not
  "concentration". All quantities are in the registry.
- `int32` encoding: physical value = raw x scale. `0x7FFFFFFF` = not
  available (no value yet, sensor missing), `0x7FFFFFFE` = error (sensor
  fault), `0x7FFFFFFD` = out of range.
- `float32` encoding (for quantities over many decades): IEEE 754, NaN = not
  available.
- Quantity code 0 is invalid; 255 is reserved for an extension.
- The scale is chosen so that the usual range fits int32 with the resolution
  the best sensor delivers. Running totals and other values that do not fit
  use [Measurement Wide](#measurement-wide-130816).
- A value with several components is either several channels, when each
  component means something alone (acceleration x/y/z), or a structured
  message (a standard PGN or a new B1S message), when only the whole means
  something (a position, an RGBW colour).

### Measurement (65280)

One scalar value of one channel. 8 bytes, single frame.

| Bytes | Field | Type |
| --- | --- | --- |
| 0..1 | Proprietary header | |
| 2 | Quantity | uint8, registry |
| 3 | Channel | uint8, local to the device, 0..252 |
| 4..7 | Value | int32 or float32, per quantity |

Sent at the channel's period (default per device; typically 1..2 s for
environment values, faster for control values) and, optionally, early on a
significant change. A channel whose sensor is missing keeps being sent with
"not available", so consumers can tell "missing" from "device gone".

### Measurement Wide (130816)

Like Measurement, with a 64-bit value. Fast packet, 13 bytes.

| Bytes | Field | Type |
| --- | --- | --- |
| 0..1 | Proprietary header | |
| 2 | Quantity | uint8 |
| 3 | Channel | uint8 |
| 4..11 | Value | int64 with the quantity's scale (not available `0x7FFF...FF`), or float64 for float quantities (NaN) |
| 12 | Reserved | `0xFF` |

### Measurement Summary (130817)

Statistics of one channel over a period (for example the motion statistics
in [motion-sensing.md](../../dev/can-mcu/motion-sensing.md)). Fast packet,
24 bytes. Values use the quantity's int32 scale; float quantities are not
summarized.

| Bytes | Field | Type |
| --- | --- | --- |
| 0..1 | Proprietary header | |
| 2 | Quantity | uint8 |
| 3 | Channel | uint8 |
| 4..5 | Period | uint16, 1 s |
| 6..7 | Sample count | uint16 |
| 8..11 | Mean | int32 |
| 12..15 | Minimum | int32 |
| 16..19 | Maximum | int32 |
| 20..23 | Standard deviation | uint32 (`0xFFFFFFFF` not available) |

### Event (130818)

Something that happened at a moment: a lightning strike, a tack, a line
snatching. Fast packet, 22 bytes.

| Bytes | Field | Type |
| --- | --- | --- |
| 0..1 | Proprietary header | |
| 2 | Event type | uint8, registry (`events`) |
| 3 | Channel | uint8 |
| 4..5 | Age | uint16, 1 ms: how long before sending the event occurred |
| 6..21 | Parameters 1..4 | int32 each, meaning and scale per event type, `0x7FFFFFFF` = not used |

### Output Status (65281)

State of an output: relay, dimmer, fan, LED channel. Sent at least every
10 s and immediately on change. 8 bytes, single frame. Load current and
similar values of the output are separate measurement channels.

| Bytes | Field | Type |
| --- | --- | --- |
| 0..1 | Proprietary header | |
| 2 | Channel | uint8 |
| 3 | Output kind | uint8, registry (`output_kinds`) |
| 4 | Flags | bit 0 on, bit 1 fault, bit 2 overcurrent, bit 3 local override, bits 4..7 reserved (1) |
| 5..6 | Level | uint16, 0.01 % (0..10000), `0xFFFF` = not applicable |
| 7 | Reserved | `0xFF` |

### Channel Description (130819)

The meaning of one channel. Fast packet, at most 76 bytes. Sent for all
channels once after the address claim, for one channel whenever its
description changes, and on request: an ISO request for 130819 addressed to
a device is answered with the descriptions of all its channels. Consumers
request it when they see a channel they do not know.

| Bytes | Field | Type |
| --- | --- | --- |
| 0..1 | Proprietary header | |
| 2 | Channel | uint8 |
| 3 | Quantity or output kind | uint8 (quantity for measurement channels, output kind for outputs) |
| 4 | Flags | bits 0..1 encoding mode (0 `b1s`, 1 `standard`, 2 `off`, 3 not available), bit 2 output channel, bit 3 the quantity has no standard PGN, bits 4..7 reserved (1) |
| 5 | Standard PGN instance | uint8, `0xFF` = none |
| 6..7 | Nominal period | uint16, 0.01 s, `0xFFFF` = on change only |
| 8.. | Location | NMEA 2000 STRING_LAU, ASCII, at most 32 characters |
| .. | Label | STRING_LAU, ASCII, at most 32 characters |

- **Location** is a Signal K path fragment, letters and digits in camelCase
  separated by dots: `inside.refrigerator`, `inside.engineRoom`, `outside`,
  `water`, `batteries.house`. Empty = unassigned (a new device).
- **Label** is free text for people: `Fridge`, `Engine room air`.

### Command (61184)

Addressed single-frame commands, for example to outputs. 8 bytes.

| Bytes | Field | Type |
| --- | --- | --- |
| 0..1 | Proprietary header | |
| 2 | Command code | uint8, registry (`commands`); bit 7 set = reply |
| 3 | Channel | uint8, `0xFF` = device |
| 4..7 | Argument | 4 bytes, per command |

Every command is answered with a reply to the sender: command code with bit
7 set, the same channel, byte 4 = result (0 done, 1 unknown command,
2 unknown channel, 3 invalid argument, 4 refused, 5 busy), bytes 5..7 `0xFF`.

### Service (126720)

Addressed fast-packet messages for configuration, calibration, and data
transfer: byte 2 is the service code (registry, `services`), the rest is
defined by the service. Configuration, calibration, and diagnosis over CAN
are designed in [CONFIGURATION.md](CONFIGURATION.md) (draft); firmware
updates will add their own services.

## Alerts

Measurement alarms (the warning / alarm / emergency rules of
`slstm32::sensors`) are sent as the standard NMEA 2000 alert messages
*(planned)*:

| PGN | Message | Use |
| --- | --- | --- |
| 126983 | Alert | State of each alert (active, acknowledged, cleared); severity: emergency alarm, alarm, warning |
| 126985 | Alert Text | Description, for example `Fridge temperature high` |
| 126984 | Alert Response | Acknowledgement from Signal K or another device |

There is no B1S alert message: alerts are already standard and Signal K
understands them. They can be switched off per device. Alerts are a
`navigation` category for forwarding purposes: chartplotters show them, so
Signal K forwards them to NMEA 2000 only deliberately.

## Timing, priorities, and bus load

| Traffic | Priority | Typical rate |
| --- | --- | --- |
| Commands and replies, alerts | 2..3 | On demand |
| Control values (output status, fast measurements) | 3..4 | Up to 10 Hz |
| Measurements | 5..6 | 0.5..1 Hz |
| Summaries, events | 6 | 1/min .. on demand |
| Channel descriptions, service, heartbeat, product information | 7 | On demand, 60 s |

A fully loaded 250 kbit/s bus carries about 1,700 eight-byte frames per
second. B1S budgets **at most 30 % (about 500 frames/s) for the whole
network**. Each product lists its typical frames per second in the registry;
the sum over an installation must stay within the budget.

## Signal K

The Signal K plugin [`src/signalk/plugin/b1s`](../../signalk/plugin/b1s/README.md)
reads B1S from its own Signal K connection on the B1S interface (own event
names, so B1S and NMEA 2000 stay apart inside Signal K; Signal K's node uses
address 150):

- Standard PGNs are decoded by Signal K itself.
- B1S messages are decoded with canboat definitions the plugin builds from
  `registry.yaml`, so canboat-based analyzers decode B1S too.
- Paths come from the quantity's Signal K template and the channel's
  location, for example quantity `temperature` (`environment.{location}.temperature`)
  with location `inside.refrigerator`. Unassigned channels appear under
  `b1s.unassigned.<device ID>.<channel>.<quantity>`.
- Devices are bound by their device ID, never by address.
- *Planned:* the plugin is the bridge to NMEA 2000: it forwards chosen
  signals in a chosen form, never everything.

## Device lifecycle

1. **First power-on:** the device ID is derived from the MCU; the device
   claims its product's preferred address and sends its channel descriptions
   with empty locations. Signal K shows it as unassigned.
2. **Commissioning** *(planned, configuration over CAN)*: the device role,
   channel locations and labels, standard-PGN instances and encoding modes,
   and alarm thresholds are set from the Pi. The Pi keeps a copy of every
   device's configuration.
3. **Replacement** *(planned)*: a new, unassigned device of the same product
   appears while a configured one is missing; the tool offers to restore the
   missing device's configuration onto it. Signal K paths stay the same.
   Calibration belongs to the physical sensor and is not copied.

## Firmware library

`src/lib/b1s` is HAL-free like `n2k` and `slstm32` and builds on both. A
device lists its measurement channels as `b1s::ChannelConfig` (channel
number, requested encoding, NMEA 2000 source, location, label), runs a
`b1s::MeasurementPublisher` as a service, and registers
`MeasurementPublisher::handleRequest` with `n2k::Node::onRequest` so that
requests for Channel Descriptions are answered. Identity values (product
code, preferred address, device class and function) come from
`b1s::product::<device>` in the generated registry header. Example:
`src/dev/can-mcu/lab-test-01/src/app.cpp`.

Cost on LT1 (product build without development tools, 4 channels): about
1.3 KiB of flash and 16 bytes of RAM. The publisher has no buffers of its
own; a Channel Description is built on the stack (76 bytes) and sent through
the node's fast-packet buffer.

After changing `registry.yaml`:

```sh
python3 src/lib/b1s/tools/generate_registry.py          # regenerate include/b1s/registry.hpp
python3 src/lib/b1s/tools/generate_registry.py --check  # verify it is current
```

## Adding to B1S

These rules are binding (AGENTS.md). They keep B1S consistent over many
devices and years.

**Before adding anything, reuse:** an existing quantity, event, output kind,
or command that means the same thing. Location and label say *where* and
*which*; they are never a reason for a new quantity.

**A new quantity, event type, output kind, command, or service:**

1. Append it to the matching list in `registry.yaml` with the next free code.
   Codes are never reused, renumbered, or redefined. Regenerate
   `include/b1s/registry.hpp` (see [Firmware library](#firmware-library)) and
   commit both.
2. Give every field: identifier (lowerCamelCase), description, and for
   quantities the SI unit as in Signal K, scale, encoding, and Signal K path
   template.
3. A quantity's unit, scale, and encoding never change once it is in the
   registry. If they must, mark the old entry `deprecated: true` and add a new
   one.

**A new B1S PGN** only when no existing message fits (structured data):

1. Take the next free PGN of the right range (single frame or fast packet,
   broadcast or addressed) and add it to `registry.yaml` and to the
   [PGN allocation](#pgn-allocation) table with its layout in this file.
2. Start with the proprietary header; use NMEA 2000 field conventions.
3. Later versions only append fields at the end. Receivers ignore extra
   bytes; senders never change the meaning of an existing byte.

**A new standard PGN** for devices: add it to `standard_pgns` in the
registry with its category (`display` or `navigation`) before a device sends
it. When in doubt, the category is `navigation`.

**A new product (device type):** add it to `products` with the next product
code, its preferred address, NMEA 2000 device class and function, and its
typical frames per second.

**Never:**

- invent a number in device code without a registry entry;
- send a standard `navigation` PGN by default;
- use a device class or function the device does not perform;
- depend on a device's address;
- put B1S-specific code into `src/lib/n2k` (NMEA 2000 only) or `slstm32`
  (generic); B1S code lives in `src/lib/b1s`.
