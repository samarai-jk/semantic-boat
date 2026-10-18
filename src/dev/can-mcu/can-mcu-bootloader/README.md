# can-mcu bootloader

The bootloader of every can-mcu device: the first 16 KB of flash. It starts
the application, or stays and receives a new one over B1S. Design, flash
layout, image header, and update services: [FIRMWARE.md](../../../lib/b1s/FIRMWARE.md).

- Register level (CMSIS only, no HAL or CubeMX), about 5.8 KB; the CMSIS
  device headers come from lab-test-01's CubeMX tree (same STM32L431).
- Clock: the 8 MHz crystal (HSE) directly, HSI16 if it does not start. CAN at
  250 kbit/s, polled; frames are sent in the order queued.
- Before starting the application it restores the clock enables exactly as
  at reset (the RTC's bus clock is on then; the application's RTC set-up
  relies on it), switches back to MSI, resets CAN, GPIO, and USART1, and sets
  the vector table.
- Log on the ST-Link virtual COM port (115200 8N1): `bootloader: starting
  application`, or the version, the reason it stays, the claimed address,
  and update steps.
- While it stays, the yellow CAN LED flashes twice a second (fast during an
  update or identify).

## Build and flash

```sh
script/build.sh can-mcu-bootloader release            # build/can-mcu-bootloader/release/...
script/build.sh lab-test-01 debug refresh flash-all    # bootloader + application
```

The bootloader is written with the ST-Link only; an update over CAN never
touches it. Both build types are optimized for size.

## Bench results (2026-10-18, LT1)

| Test | Result |
| --- | --- |
| Start of a valid application | Hand-over within milliseconds; LT1 starts normally |
| Update over CAN (`b1s_tool.py firmware`) | 73 KB in 9.9 s, 152 KB in 20.7 s (7.3 KB/s, 200-byte chunks); the whole update with restart and confirmation about 25 / 36 s |
| Update interrupted, then reset | Stays: `no valid application`; the update can be repeated |
| Corrupted image (one byte) | `firmwareEnd` refused with CRC mismatch; after a reset: `no valid application` |
| New image reset three times before it confirms | Fourth start stays: `application not confirmed` |
| Product information, NACK, `restart` command | As specified |
