# Semantic Link protocol v1

Semantic Link carries subscriptions and typed display values over a reliable
byte stream such as a debug UART or RS485. Signal K JSON is parsed by the host
gateway; the STM32 only handles the compact messages described here.

## Byte framing

Every frame is COBS encoded and followed by one `0x00` delimiter. The decoded
frame uses little-endian integers:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 1 | Protocol version, currently `1` |
| 1 | 1 | Message type |
| 2 | 1 | Sequence number, wrapping at 255 |
| 3 | 1 | Flags, zero unless specified by a message |
| 4 | 2 | Payload length |
| 6 | n | Payload, at most 240 bytes |
| 6+n | 2 | CRC-16/CCITT-FALSE over the header and payload |

CRC parameters are polynomial `0x1021`, initial value `0xffff`, no reflection,
and no final XOR. A receiver discards malformed frames and resumes at the next
zero delimiter. Sequence numbers currently aid diagnostics; realtime values
are not acknowledged because a newer value supersedes a lost one.

## Messages

| Type | Name | Direction | Payload |
| ---: | --- | --- | --- |
| `0x01` | Hello | Either | role `u8`, name length `u8`, UTF-8 name |
| `0x02` | Subscriptions begin | Device to gateway | section index `u16`, subscription count `u16` |
| `0x03` | Subscribe | Device to gateway | source index `u16`, period ms `u32`, flags `u8`, provider length `u8`, path length `u8`, provider and path UTF-8 bytes |
| `0x04` | Subscriptions end | Device to gateway | Empty |
| `0x10` | Number value | Gateway to device | source index `u16`, IEEE-754 binary32 value |
| `0x11` | Text value | Gateway to device | source index `u16`, text length `u8`, UTF-8 text |
| `0x12` | Unavailable value | Gateway to device | source index `u16` |
| `0x20` | Ping | Either | Sender uptime in ms `u32` |
| `0x21` | Pong | Either | The ping payload being answered |
| `0x30` | Config list request | Device to gateway | Empty |
| `0x31` | Config list begin | Gateway to device | list ID `u16`, item count `u16` |
| `0x32` | Config list item | Gateway to device | list ID `u16`, item index `u16`, name length `u8`, UTF-8 filename |
| `0x33` | Config list end | Gateway to device | list ID `u16` |
| `0x34` | Config request | Device to gateway | name length `u8`, UTF-8 filename |
| `0x35` | Config begin | Gateway to device | transfer ID `u16`, JSON size `u32`, JSON CRC-32 `u32`, name length `u8`, UTF-8 filename |
| `0x36` | Config chunk | Gateway to device | transfer ID `u16`, byte offset `u32`, JSON bytes |
| `0x37` | Config end | Gateway to device | transfer ID `u16` |
| `0x40` | Alert update | Gateway to device | level `u8`, flags `u8`, occurrence `u32`, ID length `u8`, title length `u8`, message length `u8`, then ID, title, and message UTF-8 bytes |
| `0x41` | Alert remove | Gateway to device | ID length `u8`, alarm ID UTF-8 bytes |
| `0x42` | Alert action | Device to gateway | action `u8`, occurrence `u32`, ID length `u8`, alarm ID UTF-8 bytes |
| `0x43` | Alert silence | Gateway to device | ID length `u8`, alarm ID UTF-8 bytes |

Hello role is `1` for a display and `2` for a gateway/simulator. Subscribe flag
bit 0 means the source is permanent. Other flag bits are reserved. Provider is
normally `signalk`; the development configuration may use `fake` while retaining
Signal K-compatible paths.

Subscription messages form an atomic snapshot. On `Subscriptions begin`, the
gateway creates a pending set. It replaces the active set only after a valid
`Subscriptions end`. The device sends a new snapshot after startup, gateway
hello, reconnection, and section changes. Permanent sources appear in every
snapshot; section sources appear only when their section is active.

A snapshot whose subscription count is zero is an explicit unsubscribe. The
gateway replaces the active set with an empty set when it receives the matching
`Subscriptions end`. A device sends this snapshot before an orderly shutdown or
reset; the gateway must not retain the previous subscriptions after the device
goes silent.

The source index is assigned by the compiled display configuration and is only
stable for that configuration. Gateways must use the current subscription
snapshot and must not persist source indices across device restarts or
configuration changes.

A display Hello invalidates the gateway's snapshot from an earlier device
session. Until a complete replacement snapshot arrives, the gateway should
periodically announce its own Hello. Both Hello and subscription snapshots are
idempotent state announcements; this recovery does not introduce an
acknowledgement or request/response transaction.

## Configuration discovery and transfer

Configuration discovery and transfer are asynchronous announcements, not an
RPC transaction. A device may send `Config list request` or `Config request`,
but it does not enter a protocol-level waiting state. A gateway may send a list
or a configuration at any time, including without a preceding request. There
are no acknowledgements, request IDs, retries, or nested response states in
version 1.

A list is assembled between matching `Config list begin` and `Config list end`
messages. A new begin message replaces an incomplete list. Items belonging to a
different list ID are ignored. The item count is informational; a bounded
device may retain fewer items. The filename is both the displayed configuration
name and the value sent in `Config request`.

A configuration is assembled between matching `Config begin` and `Config end`
messages. A new begin replaces an incomplete transfer without affecting any
list being assembled. Chunks are ordered and contiguous in version 1: the first
offset is zero and each next offset equals the number of JSON bytes already
received. The largest chunk contains 234 JSON bytes because its six-byte
transfer header must fit the 240-byte frame payload.

Senders should pace chunks for the receiving device rather than relying on
request/acknowledgement flow control. `disp-sim` uses 32-byte JSON chunks with
a 30 ms inter-frame gap so every encoded chunk fits Remote-A's 64-byte UART
receive block and leaves ample processing time. This is a gateway
implementation choice, not another protocol state or handshake.

The CRC in `Config begin` is standard CRC-32/ISO-HDLC over the complete JSON
file: reflected polynomial `0xedb88320`, initial value `0xffffffff`, and final
XOR `0xffffffff`. The device validates the declared size and CRC before passing
the UTF-8 JSON source to its configuration compiler. Activating and persisting
the compiled package are device concerns and do not add a synchronous response
exchange to this protocol.

Remote-A continues normal link processing while the requested JSON is being
transferred. After the complete transfer has passed size and CRC validation, it
sends an empty subscription snapshot, compiles the JSON into temporary memory,
and commits the package to EEPROM. A successful commit starts its ordinary reset
procedure, which closes the transport, powers down local outputs and the
E-paper controller, and resets. The freshly booted firmware then loads the
committed package and announces its new subscriptions. Compile or storage
failure leaves the old package running and publishes its subscriptions again so
the error can be shown and another transfer can be attempted.

## Alerts

Alert levels are `0` info (Signal K `alert`), `1` warning (`warn`), `2` alarm,
and `3` emergency. IDs are stable server-assigned identifiers, normally derived
from the Signal K notification path. `occurrence` increases whenever the same
condition is newly raised or deliberately repeated; retransmitting an unchanged
ID and occurrence is idempotent. Flag bits are reserved and currently zero.

Alert updates, removals, and silence messages are independent state
announcements. They may arrive without a preceding request and do not create a
request/response transaction. A gateway should resend its current active-alert
snapshot after a device hello or reconnect so a dropped frame cannot
permanently hide an active condition.

Alert action values are `0` acknowledge and `1` snooze. Acknowledge asks the
server to remove the full-screen alert globally while retaining the underlying
condition until it is rectified; snooze asks the server to remove the alert
from clients temporarily and re-announce it with a newer occurrence if the
source condition remains active. Action `2` requests global audible silence
without removing the visual alert. The gateway broadcasts `Alert silence` for
that ID so every display stops sounding it. A local-only Hide action is never
sent on the wire.

## Limits and behavior

- Maximum decoded payload: 240 bytes.
- Remote-A currently accepts Semantic Link configuration JSON up to 8,192
  bytes, lists up to
  16 files, and filenames up to 47 UTF-8 bytes. These are implementation limits,
  not framing limits. Its separate SWD staging area accepts up to 16,368 bytes.
- Provider and path lengths are each encoded in one byte, but their combined
  subscription payload must fit the frame limit.
- Text values longer than the device's value slot are truncated by the display.
- Unknown message types are ignored.
- Values for unknown or currently unsubscribed source indices are ignored.
- The protocol does not define RS485 addressing yet. Version 1 assumes one
  display per point-to-point link. Addressing can be added later using frame
  flags or an envelope without changing value and subscription payloads.

## Example development flow

`disp-sim` opens the ST-Link virtual COM port, receives a subscription snapshot,
and emits synthetic values for those paths. A future Signal K plugin implements
the gateway side of the same messages and substitutes live Signal K deltas for
the synthetic generator.
