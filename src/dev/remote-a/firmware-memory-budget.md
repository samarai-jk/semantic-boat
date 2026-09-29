# Firmware memory budget and runtime measurement

This document is a handoff for developers and AI agents adding another Semantic
Boat display, especially the planned CAN/NMEA 2000 variant. Do not assume that
a successful link means the firmware has enough RAM. Compare the link map with
runtime high-water measurements after exercising the worst application paths.

The figures below are a Remote-A Debug-build snapshot from 2026-09-28. They are
a baseline, not permanent limits; rebuild and remeasure after material changes.

## STM32L431 memory regions

Remote-A uses the STM32L431 with two separate SRAM regions:

| Region | Capacity | Current use | Notes |
| --- | ---: | ---: | --- |
| RAM1 | 48 KiB | 30,424 bytes reported by the linker | Static state, EPD framebuffer, heap and main stack |
| RAM2 | 16 KiB | 8 KiB | Resident compiled display package |
| Firmware flash | 240 KiB | 154,736 bytes | 62.96% of the firmware region |
| Config staging flash | 16 KiB | Separate from firmware | Reserved for SWD configuration upload |

The linker RAM1 figure includes its configured minimum heap and stack
reservations. The more useful map boundaries for the same build are:

```text
RAM1 start             0x20000000
static-data end        0x200070d4  (28,884 bytes, about 28.2 KiB)
heap start (_end)      0x200070d8
stack top (_estack)    0x2000c000
heap/stack arena                     20,264 bytes, about 19.8 KiB
```

RAM2 is not included in the on-screen `R` or `S` values. The current linker
places the fixed 8 KiB compiled display package at the start of RAM2, leaving
8 KiB for future explicitly placed data. A future CAN implementation may place
suitable fixed protocol pools there, but it must update and verify the linker
layout rather than expecting ordinary globals to move there automatically.

## Meaning of `R` and `S`

When Developer Mode is enabled, the top status bar shows:

```text
R19.0 S0.8
```

Both values are binary kibibytes (1 KiB is 1,024 bytes).

- `R` is the observed safe RAM1 headroom between the current heap end and the
  deepest stack address reached since monitoring began.
- `S` is the peak main-stack consumption observed since monitoring began.

They describe RAM1 approximately as follows:

```text
low address                                             high address
| static data | heap -> | R: untouched gap | <- stack | stack top
```

At startup, `DeviceMetricsService::beginStackMonitoring()` fills the unused
heap/stack arena with `0xa5a5a5a5`, leaving a small guard below the current
stack pointer. The periodic measurement scans upward from the current heap end.
The first changed word is treated as the stack low-water mark:

```text
S = stack top - observed stack low-water mark
R = observed stack low-water mark - current heap end
```

Consequences:

- Within one boot, `S` should stay constant or increase.
- Within one boot, `R` should stay constant or decrease.
- Moving from `R19.0` to `R18.8` means a later path used roughly 200 bytes more
  stack, or the heap grew by that amount.
- Resetting the MCU resets the high-water measurement.
- Interrupt handlers running after monitoring starts use the same main stack
  and are included when they establish a new low-water mark.
- The current firmware intentionally avoids normal heap allocation. If a future
  library uses `new` or `malloc`, the heap grows upward and reduces `R`.
- The values update once per second internally but appear on the EPD on its next
  render; they are not intended to force a display refresh every second.

The observed Remote-A values of `R18.8` to `R19.0` and `S0.8` are healthy for
normal runtime. Together with the unused 8 KiB of RAM2, the device currently has
about 27 KiB unused across both banks. The regions are not one interchangeable,
contiguous allocation, so do not present that sum to an allocator as one pool.

## What the measurement does not prove

The reported `S0.8` only describes paths exercised since the latest reset. It
does not prove that every firmware path uses 0.8 KiB or less.

In particular, configuration compilation has previously produced a much larger
stack peak, around 8.6 KiB. Live configuration installation now compiles and
commits the package and then resets. The new boot therefore starts a fresh
measurement and normally displays only the much smaller runtime peak. The
configuration source and compiler output reuse the 16.4 KiB EPD framebuffer,
so they do not require permanent duplicate buffers, but the compiler's call
stack still needs RAM1 while it runs.

Monitoring begins in `remote_a_app_init()`, after the earlier C runtime, HAL and
CubeMX startup path. It therefore does not measure stack used before that call.
It is also a high-water pattern measurement, not MPU protection: exhausting the
gap can corrupt memory before software has an opportunity to report `R0.0`.

## Guidance for the CAN/NMEA 2000 device

The CAN device is expected to use the same MCU and display architecture but add
CAN and a reduced NMEA 2000 stack. Much of that stack may occupy flash, but
message pools, fast-packet assembly, device tables, queues and library globals
can consume significant static RAM.

Before integrating the stack:

1. Produce a clean Debug build and record FLASH, RAM1 and RAM2 usage.
2. Preserve the Remote-A-style runtime metrics and Developer Mode header.
3. Identify every configurable CAN/N2K pool and set an explicit bounded size.
4. Prefer fixed storage and bounded queues. Do not silently introduce unbounded
   heap allocation into the firmware.
5. Consider placing long-lived fixed CAN/N2K buffers in the unused half of RAM2.
6. Verify that the display package still fits its 8 KiB RAM2 allocation.
7. Exercise high-rate CAN traffic, fast-packet reassembly, page rendering,
   alarms, menus, sleep/wake and error paths while observing `R` and `S`.
8. Test configuration download, compilation and EEPROM commit with the CAN
   stack present. Normal-runtime `S` is not a substitute for this test.
9. Inspect dropped-event, dropped-UART/CAN and queue-overflow counters as well
   as RAM. Sufficient memory does not guarantee sufficient processing time.
10. Repeat the measurements in the actual Release build intended for shipping.

Do not consume all measured headroom just because a test run survives. Keep a
deliberate reserve for unusual interrupt nesting, future protocol fields and
paths not covered by the test. As a project guideline, investigate any design
that leaves less than 8 KiB measured RAM1 headroom under stress, and do not ship
with less than 4 KiB without a documented worst-case stack analysis.

## Relevant implementation files

- Runtime measurement and displayed status:
  `src/dev/remote-a/src/device_metrics_service.cpp`
- RAM capacities and reusable installation workspace:
  `src/dev/remote-a/src/app.cpp`
- RAM1/RAM2 sections and exported boundary symbols:
  `src/dev/remote-a/cmx/STM32L431xx_FLASH.ld`
- Display configuration installation lifecycle:
  `src/dev/remote-a/src/configuration_service.cpp`
- Protocol-side shutdown and unsubscribe behavior:
  `src/dev/remote-a/src/device_shutdown_service.cpp` and
  `src/dev/remote-a/src/display_link_service.cpp`

Always verify the actual linker-script filename generated for a new device;
CubeMX naming and placement can differ between projects.
