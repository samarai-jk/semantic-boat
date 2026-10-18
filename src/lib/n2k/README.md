# n2k

A small NMEA 2000 stack for slstm32 firmware. It contains no HAL code: it runs
on an `slstm32::drivers::CanBusDriver` and is a regular `slstm32::Service`, so
any board with a CAN binding can use it. It is sized for the STM32L431 class
of devices that also run a display: about 3.3 KiB of flash and 384 bytes of
RAM, of which 223 bytes are the fast-packet transmit buffer.

## Contents

| Header | Purpose |
| --- | --- |
| `n2k/can_id.hpp` | 29-bit identifier ↔ priority, PGN, source, destination (PDU1/PDU2) |
| `n2k/name.hpp` | 64-bit NAME, and `uniqueNumberFrom()` to derive the 21-bit unique number from an MCU serial |
| `n2k/bytes.hpp` | Little-endian field and fixed-length string helpers |
| `n2k/pgn.hpp` | PGN constants |
| `n2k/node.hpp` | The node: address claim, requests, heartbeat, product information, PGN list, fast-packet transmission |
| `n2k/temperature.hpp` | PGN 130316 encoder |

## Node behavior

- **Address claim (60928).** The node claims its preferred address and
  considers it claimed after 250 ms without contention. When another node
  claims the same address, the lower NAME wins: the node re-asserts its claim
  if its NAME is lower, and otherwise moves to the next free address. Addresses
  seen on the bus count as used. With no free address it sends "cannot claim"
  from address 254. The address handler reports a changed claimed address so
  that the application can persist it and start there next time.
- **ISO request (59904).** Requests for the address claim, product information
  (126996), PGN list (126464), and heartbeat (126993) are answered. Any other
  request addressed to the node gets a NACK (ISO acknowledgement 59392);
  unsupported global requests are ignored.
- **Heartbeat (126993)** at the configured interval (default 60 s), with the
  CAN controller state. The interval field uses 0.01 s units.
- **Product information (126996)** from `NodeConfig::product`. Strings are
  padded with 0xFF.
- **PGN list (126464)**: the stack's own PGNs plus the application's lists.
- **Fast packets** are sent frame by frame as the CAN transmit queue frees up,
  so the CAN driver's queue can stay small. A fast packet that cannot make
  progress for 1 s (for example without any acknowledging node on the bus) is
  dropped and counted.
- The node owns the receive side of the CAN driver. Single frames that are
  broadcast or addressed to the node and not handled by the stack go to the
  message handler.

Before the address is claimed, only the address claim is sent; `sendSingle()`
and `sendFast()` return false.

## Usage

```cpp
constexpr std::uint32_t transmitPgns[]{n2k::pgn::temperatureExtendedRange};
n2k::NodeConfig config{
    n2k::Name{uniqueNumber, manufacturerCode, 0u, 0u, deviceFunction, deviceClass, 0u, 4u, true},
    preferredAddress,
    n2k::ProductInfo{2100u, productCode, "Model", "1.0.0", "HW 1", serial, 0u, 1u},
    transmitPgns, 1u, nullptr, 0u,
    60000u,
};
n2k::Node node{runtime, canDriver, config, logger}; // config must outlive the node
node.onMessage(&handler, context);
node.onAddressChange(&storeAddress, context);
// Register the node with makeServiceSet(...).
```

## Limitations

- No fast-packet reception yet; only single-frame messages reach the
  application.
- No commanded address (65240), configuration information (126998), or group
  function handling (126208).
- All Semantic Boat devices use manufacturer code 2046
  (`n2k::manufacturerCode`), which NMEA has not assigned to anyone; it will
  not be registered.
