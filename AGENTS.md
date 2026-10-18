# Rules for agents and contributors

These rules are **binding** for every coding agent (Claude Code, Codex, and
others) and every human contributor working in this repository. Read this file
before changing firmware. If a task conflicts with a rule, stop and ask the
user instead of working around the rule. Never weaken or remove a safeguard
described here unless the user explicitly asks for it.

## Repository essentials

- Layout, toolchain, build, and flash commands: [README.md](README.md).
- Under `src/dev/<device>/cmx/` only code inside CubeMX `USER CODE` sections is
  edited by hand; everything else is regenerated.
- `src/lib/slstm32` is HAL-free, heap-free, without exceptions and RTTI, and
  contains no board- or device-specific code. Read its
  [README](src/lib/slstm32/README.md), including the maintainer checklist,
  before changing it.
- `src/lib/n2k` and `src/lib/semantic-modules` are HAL-free as well.
- Code bound to the can-mcu board (HAL bindings for its fixed hardware) lives
  in `src/dev/can-mcu/common/`; host- and product-specific code lives in the
  device directory. Hardware descriptions:
  [can-mcu.md](src/dev/can-mcu/can-mcu.md), [host-dev.md](src/dev/can-mcu/host-dev.md),
  module notes in `src/lib/semantic-modules/`.

## Firmware code categories and build profiles (mandatory)

Development and test code must never end up in product firmware. The
repository enforces this with code categories, a build profile per firmware,
and a CMake switch. Follow all of the following.

### 1. Classify every piece of code before writing it

| Category | What it is | Examples | In product firmware |
| --- | --- | --- | --- |
| **Product** | What the device does in the field | Drivers, NMEA 2000, watchdog, power monitor, crash record, health record, calibration *data* and its store, settings | Yes |
| **Service** | Diagnosis and maintenance of a product | Log output, console, `cal`, `health`, `supply`, read-only I2C scan | Yes, but each exposure (for example the console on the service port) is a product decision; ask the user |
| **Dev/test** | Bring-up, experiments, test aids | Provoked faults (`trap`), raw register writes (`i2c write`), hardware and module test services, simulated or background load, fake data, test tones and patterns, temporary watches and debug prints | **Never** |

If you are unsure, the code is **dev/test**. Anything that can provoke
faults, write raw registers, erase or overwrite storage, disable protections,
or fake data is always dev/test.

### 2. Put dev/test code only where the build can leave it out

- Device-specific dev/test code goes into `src/dev/<board>/<device>/src/dev/`.
  Dev/test code shared by several devices of a board goes into
  `src/dev/<board>/common/dev/` (create it when needed, with its own source
  list in that board's CMake helper).
- Libraries under `src/lib/` contain product and service code only.
- Every dev/test source file starts with this marker comment:

  ```cpp
  // DEVELOPMENT AND TEST CODE: compiled only with SB_DEV_TOOLS (AGENTS.md,
  // "Firmware code categories and build profiles"). Never reference this from
  // product code.
  ```

### 3. Declare the build profile of every firmware

Every firmware target calls `sb_firmware_profile()` from
[`cmake/firmware-profile.cmake`](cmake/firmware-profile.cmake) exactly once:

```cmake
include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/firmware-profile.cmake)
sb_firmware_profile(${TARGET} LAB            # or PRODUCTION
    DEV_SOURCES  src/dev/dev_tools.cpp ...   # only with development tools
    STUB_SOURCES src/dev_tools_disabled.cpp) # only without them
```

- `LAB`: a lab or bring-up device (for example lab-test-01). Development tools
  default to on.
- `PRODUCTION`: a product (for example remote-a). Development tools default to
  off. **A Release build of a PRODUCTION firmware with development tools stops
  at configure time.** Do not weaken this check.
- The cache variable `SB_DEV_TOOLS` (`AUTO`, `ON`, `OFF`) overrides the default:
  `-DSB_DEV_TOOLS=OFF` checks that a lab device also builds as a product;
  `-DSB_DEV_TOOLS=ON` debugs a product with its tools (Debug builds only).
- Dev/test sources are listed **only** under `DEV_SOURCES`, never in the
  normal `target_sources()`.

### 4. Connect dev/test code through a seam, never with `#if`

- Product code reaches dev/test code only through an interface header in the
  product tree, implemented twice: by the dev/test code and by a no-op
  stand-in (`STUB_SOURCES`). Reference implementation: lab-test-01's
  [`dev_tools.hpp`](src/dev/can-mcu/lab-test-01/src/dev_tools.hpp),
  `src/dev/dev_tools.cpp`, and `src/dev_tools_disabled.cpp`.
- Product and service sources must not include anything from a `dev/`
  directory and must not contain `#if SB_DEV_TOOLS` blocks.
- `slstm32::devTools` and `slstm32::production` from
  [`slstm32/build_profile.hpp`](src/lib/slstm32/include/slstm32/build_profile.hpp)
  are allowed only for markers and compile-time guards (`if constexpr`,
  `static_assert`).

### 5. Keep the markers

A firmware built with development tools must be recognizable at runtime:

- the startup log contains `DEV BUILD: development and test tools compiled in`;
- the NMEA 2000 product information software version ends in `-dev`.

All Semantic Boat firmware, lab and product alike, uses NMEA 2000
manufacturer code 2046 (`n2k::manufacturerCode`). It is not assigned by NMEA
and will not be registered; the project's CAN network is kept separate from
NMEA 2000 networks. Never hard-code the number elsewhere.

### 6. Temporary aids

A one-off test aid (a watch on some inputs, extra debug output for a single
test) is dev/test code: put it into the `dev/` directory and remove it once
the test is done. Say in the commit message that it is temporary or that it
was removed.

### 7. New devices

When a new device starts from lab-test-01 (or any other device), copy only
product and service code, create its own seam and stand-in, declare its
profile (`PRODUCTION` for a product), and replace the lab identity (NMEA 2000
product code, model strings, device class and function).

### 8. Verify before you finish

- Build the firmware as configured (`script/build.sh <firmware> debug`).
- Lab devices: also build without development tools, for example
  `cmake --preset <firmware>-release -B build/<firmware>/product-check -DSB_DEV_TOOLS=OFF`
  followed by `cmake --build build/<firmware>/product-check`.
- Product devices: the Release preset must build without development tools.
- For a product release, `strings <elf> | grep -E "DEV BUILD|-dev"` must find
  nothing.

## Working with the bench hardware

- Only one program can use the ST-Link debug interface at a time: flashing,
  `script/swd-probe.ps1`, and a VS Code debug session exclude each other.
- Only one program can own the ST-Link serial port: `script/serial-log.ps1`
  writes `build/logs/serial.log` and accepts console commands (typed, or as
  lines in `build/logs/serial-command.txt`). `script/monitor.ps1` shows the
  events of that log and may run any number of times.
- `src/tools/can-bench` is the CAN / NMEA 2000 counterpart on the PC.
- A brand-new STM32L4 boots its bootloader until the first power cycle after
  the first flash.
