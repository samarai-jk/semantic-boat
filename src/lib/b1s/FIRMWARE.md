# B1S firmware update over CAN

Part of [B1S](README.md). Decided 2026-10-18: one application slot and a
small bootloader that speaks B1S (two slots would leave too little room:
the network display needs about 110..115 KB). remote-a has no bootloader.

**Status:** implemented and tested on LT1 (2026-10-18): bootloader
[`src/dev/can-mcu/can-mcu-bootloader`](../../dev/can-mcu/can-mcu-bootloader/README.md)
(with the bench results), LT1 as the first application, updates with
`src/tools/can-bench/b1s_tool.py firmware`. Planned: updates from the devices
page.

## Flash layout (can-mcu, STM32L431, 256 KB, 2 KB pages)

| Address | Size | Content |
| --- | --- | --- |
| 0x0800 0000 | 16 KB | Bootloader; never written by an update |
| 0x0800 4000 | 224 KB | Application, starting with its vector table and, at offset 0x200, the image header |
| 0x0803 C000 | 16 KB | Reserved for device data (display configurations of a network display); never written by an update |

## Image header

64 bytes at application offset 0x200, little-endian. The firmware defines
it (`can_mcu/image_header.hpp`); a post-build step
(`src/lib/b1s/tools/stamp_image.py`) fills in size and CRC, in the ELF file
and the binary alike, so images flashed with the ST-Link are valid too.

| Offset | Field | Content |
| --- | --- | --- |
| 0 | magic | `B1S!` (0x21533142) |
| 4 | header version | 1 |
| 6 | board | 1 = can-mcu V1 (registry `boards`) |
| 8 | product code | registry `products` |
| 10 | reserved | 0xFFFF |
| 12 | image size | bytes from the application start, a multiple of 8 |
| 16 | image CRC | CRC-32 (zlib) of the image, with these four bytes counted as 0xFF |
| 20 | version | major << 24, minor << 16, patch << 8, bit 0: development build |
| 24 | version text | 40 characters, NUL-padded, e.g. `0.1.0-dev Oct 18 2026` |

## Boot

At every reset the bootloader runs first. It starts the application unless:

- the application asked for an update (RTC backup register 7 holds
  `0xB007B007`; cleared on reading);
- the image is not valid: wrong magic, header version, or board, a size
  outside the slot, or a CRC mismatch;
- a new image was installed and the application has not confirmed it after
  three starts (RTC backup register 8: magic in bits 16..31, starts in bits
  0..7). An application confirms once it has run normally for 10 s
  (`can_mcu::confirmFirmware()`); a device that crashes before that ends in
  the bootloader instead of a restart loop.

Before starting the application the bootloader returns clocks and the
peripherals it used to their reset state and sets the vector table to the
application.

The RTC backup registers survive a reset, not a power loss without a backup
battery; after a power loss an unconfirmed image gets three new starts.

## Bootloader on B1S

While it stays, the bootloader is a minimal B1S node:

- NAME with the device's own unique number (from the MCU), so the Pi sees
  the same device ID; product `can-mcu-bootloader` (class 10 "system
  tools", function 130); preferred address 240, moving up to 251 on
  contention.
- Answers ISO requests for its address claim and product information
  (model `Semantic Boat can-mcu bootloader`, software = bootloader
  version, model version = why it stays: `update requested`,
  `no valid application`, `application not confirmed`).
- Heartbeat every 10 s; the yellow CAN LED blinks twice a second.
- Services (126720, layouts below) and the Command `restart`.

## Update services

Requests and replies as in [CONFIGURATION.md](CONFIGURATION.md#service-message-126720)
(header, service code, sequence number; replies add the status).

| Code | Service | Request fields | Reply fields |
| --- | --- | --- | --- |
| 5 | `firmwareBegin` | the image header (64 bytes) | status, largest data chunk (uint8) |
| 6 | `firmwareData` | offset (uint32), data (at most 200 bytes; a multiple of 8 except in the last chunk) | status, next expected offset (uint32) |
| 7 | `firmwareEnd` | — | status; on success the device starts the new image 100 ms later |

Further status codes: 7 wrong board, 8 image too large, 9 flash error,
10 unexpected offset, 11 CRC mismatch.

`firmwareBegin` erases the first page, so an interrupted update never leaves
a startable image. Data arrives in order; the bootloader erases each page
when the image first reaches it, programs and verifies double words.
`firmwareEnd` checks the CRC of the whole image against the header, marks
the image as new (unconfirmed), and restarts.

An application enters the bootloader with the Command `enterBootloader`
(after its reply). Images are sent from the Pi with
`src/tools/can-bench/b1s_tool.py firmware <address> <image.bin>` (the
application's or the bootloader's address): it enters the bootloader, finds
it by the device ID, sends the image in 200-byte chunks (one request at a
time, three tries, resuming at the offset the device reports), and checks
that the new application runs longer than its confirmation time. Later the
devices page.

## ST-Link

- `build.sh <device> ... flash` programs the application only (stamped ELF
  at 0x0800 4000); the bootloader stays.
- `build.sh <device> ... flash-all` programs bootloader and application (new
  boards, after a chip erase).
- `build.sh <device> rescue` connects under reset (NRST on the debug
  header) for firmware that disables the debug pins or sleeps at once.
- Debugging works as before: after a reset the bootloader starts the
  application within milliseconds.
- **Never** set readout protection level 2 or write option bytes from
  firmware: level 2 is the only way to lock out the ST-Link for good
  (AGENTS.md).
