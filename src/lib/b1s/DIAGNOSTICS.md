# B1S diagnostic sessions

A diagnostic session is a high-rate recording of a situation worth looking
at afterwards: an engine start first, later for example a bilge pump run or
a windlass pull. The device records it into RAM, keeps it until the Pi has
fetched it, and the Pi stores every session and shows it in the
**Diagnostic Sessions** app (Signal K, `@semantic-boat/diagnostic-sessions`).

| Part | Where | Generic |
| --- | --- | --- |
| Recording: series at rates that change by phase, lead-in, events, fixed memory | `slstm32::sensors::Recorder` | any device |
| B1S meaning, byte stream, services | `b1s::DiagnosticSessions`, `ServiceServerConfig::diagnostics` | any B1S device |
| When to record, which series, which rates | the device (engine-mon: `updateStartSession`) | no |
| Fetching, storing, key numbers | hub plugin `src/b1s/diagnostics.js` | yes; key numbers per kind |
| Viewer | `semantic-boat-diagnostic-sessions` (Signal K webapp) | yes |

## Kinds

Registry `diagnostic_sessions`: the code, the names of its **phases** (phase
events carry the code, 1 = the first), the **series roles** (a device
records its series in this order, so the Pi knows which is which), and the
**input roles** of its state events.

| Code | Kind | Phases | Series | Inputs |
| --- | --- | --- | --- | --- |
| 1 | `engineStart` | waiting, waitingLong, cranking, runningUp | batteryVoltage, starterVoltage, alternatorExcitation, oilPressureSender, engineSpeed | ignition, starter, fuelStop, gearReverse, gearForward, preheat |

## Byte stream

A complete session is one byte stream, little-endian:

**Header** (24 bytes): `"B1D"`, version 1, kind, flags (bit 0: truncated,
the memory ran out), end reason (1 finished, 2 time limit, 3 memory full,
4 aborted), series count, session number (uint16, counts since the device
started), event count (uint16), start (unix seconds, uint32; 0 = the
device's clock was not set), duration (ms, uint32), sample bytes (uint32).

**Series** (12 bytes each): B1S channel, quantity (registry code), divisor
(uint16), SI per stored unit (float32), SI offset (float32): SI value =
sample x scale + offset.

**Events** (12 bytes each): time (ms from the start, int32), id, kind, aux,
0, value (int32):

| Kind | id | aux | value |
| --- | --- | --- | --- |
| 0 state | B1S channel | input role (1 = the first) | 0 off, 1 on |
| 1 snapshot | B1S channel | quantity code | the channel's B1S value at that moment |
| 2 phase | 0xFF | phase code | 0 |

**Samples**: segments, one per phase (the first is the lead-in, before the
trigger, with a negative start): start (ms, int32), tick (ms, uint16),
ticks (uint16), one multiple per series (uint8; the series is sampled every
`multiple` ticks, 0 = not in this segment), then for every tick k the
int16 samples of the series whose multiple divides k, in series order.
`-32768` = no value. The time of a sample is its tick; a late sample is
taken at the next main loop pass (jitter of a few ms).

## Services (126720)

| Code | Service | Request fields | Reply fields |
| --- | --- | --- | --- |
| 9 | `diagnosticSessionInfo` | – | session number (uint16), kind, state (0 none, 1 recording, 2 complete), size (uint32, complete only), start (unix s, uint32), duration (ms, uint32) |
| 10 | `diagnosticSessionRead` | session number (uint16), offset (uint32), length (uint8, at most 200) | session number, offset, count (uint8), bytes |
| 11 | `diagnosticSessionRelease` | session number (uint16) | session number |

Status as for parameters: 0 done, 3 invalid value (wrong number or offset),
5 busy (still recording). A device keeps one session: a new one replaces a
complete one that was not fetched. A device without sessions does not
answer.

The Pi (hub plugin) asks every B1S device for its session every 15 s,
reads a complete one in 200-byte pieces (a 5 KiB session in about 2 s),
stores it, and releases it. A device that does not answer is asked again
after 10 minutes (a firmware update may have added sessions).

## On the Pi

Every session is a JSON file in the plugin's data folder
(`~/.signalk/plugin-config-data/semantic-boat-devices/diagnostic-sessions/`):
the decoded series (time and SI value), events, phases, the key numbers of
its kind, and the raw bytes (base64) for a later decoder. Routes (admin):
`/plugins/semantic-boat-devices/diagnostics/sessions` (list) and
`.../sessions/<name>`.

**Engine start key numbers:** battery at rest (lead-in), preheat time and
the battery drop while preheating (glow plug current, compared over
starts), per crank attempt its duration, lowest battery and starter
voltage, the largest battery minus starter difference (voltage lost in the
starter cables) and the compression pulses per second (dips of the starter
voltage, one per compression stroke: cranking speed = pulses x 120 /
cylinders), the time from the first crank to running, the time until the
alternator charges (D+ above 13 V), and the idle speed.

## engine-mon

Triggers, phases, rates, and memory: see `updateStartSession` and the
comment above `startSeries` in `src/dev/can-mcu/engine-mon/src/app.cpp`;
in short:

- **Start:** ignition, preheat or starter on, all three off before, the
  engine not running. The 2 s before come from a 10 Hz lead-in.
- **Phases:** waiting 10 Hz; after 20 s of waiting 2 Hz; cranking (starter
  on) battery and starter voltage 100 Hz, D+, oil sender and rpm 20 Hz;
  running up 10 Hz.
- **End:** the engine has run 10 s, or ignition, preheat and starter are
  off for 3 s (no start), or 3 minutes, or the 10 KiB are full. All crank
  attempts are in one session.
- **During a session** the input modules scan only A10, A12, A13 (AD1) and
  A20 (AD2) at 860 samples/s (about 100 per second per input on AD1).
- Snapshot at the start: coolant sender, heat exchanger, dry exhaust,
  engine room, engine hours, gearbox oil. Switch inputs as state events.
