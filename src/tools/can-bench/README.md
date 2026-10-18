# can-bench

Bench CAN / NMEA 2000 monitor for can-mcu firmware, built on python-can. It
decodes NMEA 2000 identifiers, reassembles fast packets, and decodes the PGNs
used by the firmware (address claim, ISO request and acknowledgement,
heartbeat, product information, PGN list, temperature, humidity) and the
[B1S](../../lib/b1s/README.md) messages, which it decodes from the B1S
registry (`src/lib/b1s/registry.yaml`, needs PyYAML). From its own source
address (153) it discovers the nodes with a global address-claim request and
can send requests and contend for addresses.

## Setup

```sh
python -m venv build/tools/can-venv
build/tools/can-venv/Scripts/python -m pip install -r src/tools/can-bench/requirements.txt
```

On Linux use `build/tools/can-venv/bin/python` and a SocketCAN interface
(`--interface socketcan --channel can0`).

## Usage

```sh
python src/tools/can-bench/can_bench.py                 # listen until Ctrl+C
python src/tools/can-bench/can_bench.py --duration 12 --request 126996 --request 126464
python src/tools/can-bench/can_bench.py --duration 12 --request-unsupported   # expects a NACK
python src/tools/can-bench/can_bench.py --duration 8 --conflict --quiet
```

`--conflict` sends two address claims from the address of the last node that
claimed: first with the highest possible NAME (the node must re-assert its
claim), then with NAME 0 (the node must move to another address).
`--quiet` prints only address claims and acknowledgements.

The default interface is `candle`, which drives candleLight/gs_usb adapters
(USB ID 1D50:606F) on Windows through WinUSB; the first adapter found is
used. Its 160 MHz clock allows no exact 87.5 % sample point at 250 kbit/s in
python-can's timing search, so the tool uses 85 % (`--sample-point`), which
is well within tolerance on a short bench bus.

## Bench wiring

A two-node bus needs termination and a common ground. A short bench cable
works with a single 120 Ω terminator. The can-mcu RJ45 has no direct ground
contact (see [can-mcu.md](../../dev/can-mcu/can-mcu.md#power-path)); on the
bench the ST-Link USB cable and the adapter's USB cable share the PC ground.

## Raspberry Pi bench

The Raspberry Pi has two MCP2515 CAN controllers (16 MHz crystal) on SPI1,
both brought up by systemd-networkd at 250 kbit/s, sample point 87.5 %:

| Interface | MCP2515 | Wired to |
| --- | --- | --- |
| `can1` | `spi1.1`, interrupt GPIO22 | [B1S](../../lib/b1s/README.md) backbone, with LT1 (terminated at both ends) |
| `can0` | `spi1.2`, interrupt GPIO13 | NMEA 2000 port, not connected on the bench |

Identified on 2026-10-18: with LT1 on the backbone, `can1` received its
address claim and PGNs 130316, 130313, and 127501 from address 36, and LT1
went error-active with its frames acknowledged; `can0` saw nothing. An
unconnected `can0` sits in ERROR-PASSIVE from noise on the open lines; that
is expected.

Use the backbone for bench work:

```sh
python3 -m venv build/tools/can-venv
build/tools/can-venv/bin/python -m pip install -r src/tools/can-bench/requirements.txt
build/tools/can-venv/bin/python src/tools/can-bench/can_bench.py --interface socketcan --channel can1
candump -t d can1
```

Do not transmit on `can0` once it is connected to a boat's NMEA 2000 network.

The Signal K server on the Pi brings canboat's reference decoder, which
checks the firmware's encoding independently of this tool:

```sh
timeout 10 candump -L can1 | /usr/lib/node_modules/signalk-server/node_modules/.bin/analyzerjs
```

Signal K listens on both: `can0` as an NMEA 2000 connection, `can1` as the
B1S connection with its own event names and address 150, read by the
[B1S plugin](../../signalk/plugin/b1s/README.md).

### LT1 bench result (2026-10-18)

LT1 (debug build with development tools) on the backbone, `can1` on the Pi:

| Check | Result |
| --- | --- |
| Address claim on global request | Answered, address 36, NAME 0xC0968200FFD08466 (mfr 2046, class 75, function 130) |
| 126996 Product Information | Model `Semantic Boat LT1`, software `0.1.0-dev ...`, version `can-mcu V1.0 + host-dev` |
| 126464 PGN List | Transmit 59392, 60928, 126464, 126993, 126996, 130316, 130313, 127508, 127501; receive 59392, 59904, 60928 |
| Request for an unsupported PGN (65000) | NACK (59392) to the requester |
| `--conflict` | Higher NAME: re-asserts 36; lower NAME: moves to 37 and stores it in EEPROM |
| 130316 / 130313 | canboat decodes temperatures (instances 0..2, inside) and humidity (instance 0, inside) to the same values as the LT1 log |
| 127501 (development background load) | canboat decodes bank 0, indicators 1..4 (input module DI0..DI3), 5..28 not available |
| 135 s statistics | 3.0 frames/s, 0 error frames, 0 send failures, 126993 heartbeat every 60 s without gaps; LT1 error-active, TEC/REC 0 |

127508 (battery status, module voltages) is in the PGN list but is only sent
while the input module is connected; it was not connected.
