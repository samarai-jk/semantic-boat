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

Hello role is `1` for a display and `2` for a gateway/simulator. Subscribe flag
bit 0 means the source is permanent. Other flag bits are reserved. Provider is
normally `signalk`; the development configuration may use `fake` while retaining
Signal K-compatible paths.

Subscription messages form an atomic snapshot. On `Subscriptions begin`, the
gateway creates a pending set. It replaces the active set only after a valid
`Subscriptions end`. The device sends a new snapshot after startup, gateway
hello, reconnection, and section changes. Permanent sources appear in every
snapshot; section sources appear only when their section is active.

The source index is assigned by the compiled display configuration and is only
stable for that configuration. Gateways must use the current subscription
snapshot and must not persist source indices across device restarts or
configuration changes.

## Limits and behavior

- Maximum decoded payload: 240 bytes.
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
