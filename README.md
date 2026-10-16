# Semantic Boat

Semantic Boat is a C/C++ STM32 firmware and hardware monorepo. STM32CubeMX owns
the MCU initialization in each device's `cmx/` directory; application code and
reusable drivers live outside generated code.

## Layout

- `hardware/<board>/` — PCB design, mechanical data, and old hardware prototypes
- `src/dev/<device>/cmx/` — CubeMX `.ioc`, generated HAL/CMSIS code, and generated CMake target
- `src/dev/<device>/` — hand-written C++ firmware for one device
- `src/lib/slstm32/` — heap-free reusable STM32 application framework and drivers
- `src/lib/semantic-display/` — portable configured-display compiler and runtime
- `src/lib/semantic-modules/` — drivers for Semantic Boat peripheral modules (for example 12v-ad-input-iso)
- `src/lib/n2k/` — small NMEA 2000 stack on slstm32 (address claim, requests, heartbeat, product information)
- `src/lib/` — other first- or third-party libraries

Only code inside CubeMX `USER CODE` sections should be hand-edited under
`cmx/`. Device code consumes the generated `stm32cubemx` interface target.

## Toolchain and build

Required build tools are CMake 3.22+, Ninja, and the Arm GNU Embedded Toolchain
(`arm-none-eabi-gcc`, `arm-none-eabi-g++`, `arm-none-eabi-objcopy`, and
`arm-none-eabi-size`) on `PATH`.

```sh
cmake --preset remote-a-debug
cmake --build --preset remote-a-debug
```

Firmware names are directory names below `src/dev` or `src/dev/<board>`:
`remote-a` and `lab-test-01` (can-mcu + host-dev, see
[`src/dev/can-mcu/lab-test-01/README.md`](src/dev/can-mcu/lab-test-01/README.md)).

On Windows, the root wrapper takes the firmware name first:

```bat
script\build.bat remote-a
script\build.bat remote-a debug refresh
script\build.bat remote-a flash
```

### Linux / Raspberry Pi

On Debian or Raspberry Pi OS, install the native ARM cross-compiler packages:

```sh
sudo apt-get update
sudo apt-get install cmake ninja-build gcc-arm-none-eabi \
	libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib openocd
```

The shared presets use schema version 6 and require CMake 3.25 or newer.
The compiler packages above also provide the embedded C/C++ headers, runtime
libraries, and binutils; a compiler alone is not sufficient.

The Linux wrapper accepts the same arguments as the Windows wrapper and can
be invoked from any working directory:

```sh
./script/build.sh remote-a
./script/build.sh remote-a debug refresh
```

Release is the default. Without `refresh`, the wrapper performs a clean build;
`refresh` builds incrementally. VS Code's build tasks select this wrapper on
Linux and retain the batch wrapper on Windows.

After connecting an ST-Link and the device, build and flash with:

```sh
./script/build.sh remote-a debug refresh flash
```

`update` is shorthand for `refresh flash`. OpenOCD uses `interface/stlink.cfg`
and `target/stm32l4x.cfg`, programs the ELF, verifies it, and resets the device.
It defaults to the executable on `PATH` and its installed scripts. Set
`OPENOCD` to override the executable and `OPENOCD_SCRIPTS` to override the
scripts directory. Debian's OpenOCD package supplies USB udev rules; reconnect
the probe after installation before diagnosing access errors.

The handwritten `src/lib/slstm32/src/drivers/` and
`src/lib/slstm32/include/slstm32/drivers/` directories must be committed along
with the rest of the library. The vendor `Drivers/` ignore rule can also match
lowercase `drivers/` on Windows; explicit exceptions protect these directories.
If CMake reports missing driver sources in a fresh checkout, recover and commit
both directories from the original development machine before building.

The build emits an ELF file plus `.hex` and `.bin` images beneath
`build/remote-a/debug/`.

Display configuration, EEPROM storage, and SWD upload are documented in
[`src/lib/semantic-display/README.md`](src/lib/semantic-display/README.md).
The current STM32L431 RAM baseline, runtime `R`/`S` measurements, and guidance
for the planned CAN/NMEA 2000 device are documented in
[`doc/firmware-memory-budget.md`](doc/firmware-memory-budget.md).

## Display simulator

The Linux launcher requires Node.js 20 or newer. Install its dependencies once
from the repository root:

```sh
npm ci --prefix src/tools/disp-sim
```

Connect the ST-Link virtual COM port and start the simulator with
[`script/sim.sh`](script/sim.sh):

```sh
./script/sim.sh
./script/sim.sh --list
./script/sim.sh --port /dev/serial/by-id/usb-STMicroelectronics_STLINK-V3_004F00353234510537333934-if02
```

Without arguments it attempts to select a unique serial port. Use `--list` to
find ports and `--port` to select one explicitly. The launcher works from any
working directory when invoked by its absolute path and forwards all simulator
arguments, including `--help`, `--verbose`, and `--config-dir`. Relative config
directories are resolved from `src/tools/disp-sim`. Stop with `Ctrl+C`; run only
one simulator per serial port.

Configuration selection and alert controls are documented in
[`src/tools/disp-sim/README.md`](src/tools/disp-sim/README.md).

## Signal K plugins

Signal K plugin development lives under `src/signalk/plugin/`. The
[N2K simulator](src/signalk/plugin/n2k-simulator/README.md) provides a local
encoded NMEA 2000 instrument feed and an alternative direct Signal K output
for off-boat hardware and alarm testing. Use a separate development server,
never live navigation data. Its [roadmap](src/signalk/plugin/n2k-simulator/PLAN.md)
tracks the planned helm UI, scenarios, and hardware transports.

## Debugging remote-a

Install the recommended Cortex-Debug extension, connect the ST-Link probe, and
select **remote-a: Debug (OpenOCD)** in VS Code's Run and Debug view. The launch
configuration performs an incremental Debug build before programming and
stopping at `main`.
