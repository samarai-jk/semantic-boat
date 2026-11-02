# remote-a bootloader

The bootloader of the remote-a displays: the first 16 KB of flash. It starts
the application, or stays and receives a new one over RS485. Flash layout,
image header, and boot rules are those of the can-mcu bootloader
([FIRMWARE.md](../../lib/b1s/FIRMWARE.md)) with board `remoteA`; the messages
are semantic-link's ([PROTOCOL.md, "Firmware update"](../../lib/semantic-link/PROTOCOL.md#firmware-update)).

- Register level (CMSIS only, no HAL or CubeMX), about 7 KB. It links the
  semantic-link protocol (decoder and addressed endpoint, so retries get the
  cached reply) and remote-a's settings record reader.
- Clock: the 8 MHz crystal (HSE), HSI16 if it does not start. RS485 on USART2
  (PA2/PA3, driver enable PA1 by the USART) at 115200 baud, polled; the
  receiver is off while it sends (the transceiver hears its own reply).
- RS485 address: handed over by the application in RTC backup register 10
  (`remote_a/bootloader.hpp`), else read from the display settings in the
  EEPROM (I2C2), else 1.
- The image CRC is computed while the data arrive, so `Firmware end` answers
  within the reply window; every chunk is read back after programming.
- Stays when the application asked for it, when the image is invalid, or when
  a new image has not confirmed itself within three starts. Log on the
  ST-Link virtual COM port (USART1, PA9); the signal LED (PB12) flashes twice a
  second while it waits, fast during an update.

## Build, flash, update

```sh
script/build.sh remote-a-rs485 release flash-all   # once per display, ST-Link: bootloader + application
script/remote-a-update.sh remote-a-1               # from then on: build and update over RS485
script/remote-a-update.sh all                      # every display that is online
python3 src/tools/remote-a-update.py status remote-a-2
```

`flash` alone (ST-Link) writes the application only; the bootloader stays.
The update goes through the Signal K RS485 plugin (it owns the serial port)
and its local control socket.

## Bench results (2026-11-02, display 2 on the boat bus)

| Test | Result |
| --- | --- |
| Start of a valid application | Hand-over within about 0.5 s (CRC check at 8 MHz) |
| Update from the running application, 173 KB (-O3 build) | 68 s in all: restart into the bootloader, transfer (232-byte chunks, about 66 ms each), start, 10 s confirmation |
| Test image that cannot start | Written and checked; the bootloader stays with "no valid application" and accepts the next update |
| Before the receiver was switched off while sending | Every request's first try was lost (echo overrun), 4 min per update |
