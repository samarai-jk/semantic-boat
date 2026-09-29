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

On Windows, the root wrapper takes the firmware name first:

```bat
script\build.bat remote-a
script\build.bat remote-a debug refresh
script\build.bat remote-a flash
```

The build emits an ELF file plus `.hex` and `.bin` images beneath
`build/remote-a/debug/`.

Display configuration, EEPROM storage, and SWD upload are documented in
[`src/lib/semantic-display/README.md`](src/lib/semantic-display/README.md).
The current STM32L431 RAM baseline, runtime `R`/`S` measurements, and guidance
for the planned CAN/NMEA 2000 device are documented in
[`doc/firmware-memory-budget.md`](doc/firmware-memory-budget.md).

## Debugging remote-a

Install the recommended Cortex-Debug extension, connect the ST-Link probe, and
select **remote-a: Debug (OpenOCD)** in VS Code's Run and Debug view. The launch
configuration performs an incremental Debug build before programming and
stopping at `main`.
