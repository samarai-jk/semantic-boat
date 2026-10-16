# can-bench

Bench CAN / NMEA 2000 monitor for can-mcu firmware, built on python-can. It
decodes NMEA 2000 identifiers, reassembles fast packets, and decodes the PGNs
used by the firmware (address claim, ISO request and acknowledgement,
heartbeat, product information, PGN list, temperature). From its own source
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
