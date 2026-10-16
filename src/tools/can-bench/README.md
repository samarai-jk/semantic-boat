# can-bench

Bench CAN monitor for can-mcu firmware, built on python-can. It prints every
frame, decodes the lab-test-01 raw test frame (ID `0x18FF0042`) and checks its
sequence, can transmit its own test frame (`0x18FF0099`) once per second, and
prints a summary on exit.

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
python src/tools/can-bench/can_bench.py --send --duration 30
python src/tools/can-bench/can_bench.py --quiet         # only gaps and summary
```

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
