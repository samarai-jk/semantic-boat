# engine-mon

Marine diesel engine monitor: a [can-mcu](../can-mcu.md) with an engine host
board. Until that host exists, it runs on [host-dev](../host-dev.md) on the
bench, like lab-test-01. It is a **product** (build profile `PRODUCTION`).

**Status (2026-10-20):** first version. It brings up the board, the host
inputs and outputs, B1S with configuration over CAN, and the health record.
Measured so far: the can-mcu's own temperatures (enclosure, MCU die). The
engine's own channels (rpm, temperatures, pressures, alarm outputs) follow
once the sensors are chosen. Not yet run on hardware.

## Host I/O

| Function | MCU | host-dev header | EXTI | Notes |
| --- | --- | --- | --- | --- |
| BTN0, BTN1 | PB10, PB11 | BF5, BF6 (frontends BTN_FUN_4, BTN_FUN_5) | 10, 11 | Reachable from outside the case; acknowledge alarms |
| AI0..AI2 | PA4, PA5, PA6 | J1.2, J1.4, J1.3 | – | ADC1 IN9..IN11, 0..3.3 V, sampled by `McuMonitor` |
| DO0..DO2 | PA8, PA9, PA10 | J2.4, J2.5, J2.6 | – | TIM1 CH1..CH3, PWM mode 1, active high, 1 kHz; duty 0 = off, 1 = on |
| OW0, OW1 | PB12, PB2 | J1.7, J1.15 | – | 1-Wire, open-drain; 4.7 kΩ pull-up on the host |
| DI0 | PA0 | J2.7 | 0 | Also TIM2_CH1: the rpm input |
| DI1 | PA1 | J2.9 | 1 | Also TIM2_CH2 |
| DI2, DI3 | PA3, PA7 | J1.11, J1.6 | 3, 7 | |
| DI4, DI5 | PB4, PB5 | J1.16, J1.19 | 4, 5 | Frontend pins F3 and FP, bridges open |
| DI6, DI7 | PC13, PA15 | J1.12, J1.14 | 13, 15 | Frontend pins F0 and F1, bridges open |
| Spare | PB13, PB14 | J1.8, J1.9 | – | Kept for a second I2C bus (I2C2 SCL/SDA, AF4) |
| Spare | PA2 | J1.10 | 2 | On host-dev wired to the remote LED (300 Ω) |
| HOST_POWER | PB15 | CN3 on the can-mcu | – | 8 A switched 12 V (can-mcu feature) |
| I2C | PB8, PB9 | I2C connectors | – | I2C1 with the EEPROM (0x52) and TMP75 (0x4F) |

- **Digital inputs** each have their own EXTI line, so any of them can count
  pulses (`slstm32::drivers::FrequencyInput` works from an edge interrupt).
  They are inputs with pull-up; the firmware reads the pin level. Intended
  source: two [12v-ad-input-iso](../../../lib/semantic-modules/12v-ad-input-iso.md)
  modules (four push-pull 3.3 V outputs each, **low while 12 V is present**,
  verified at 750 Hz on LT1). An unplugged input reads high.
- **Analog inputs** take 0..3.3 V: resistive engine senders need a divider
  or pull-up on the host.
- **Outputs** float until the timer starts; the host needs pull-downs so
  loads stay off during reset.

**can-mcu bridges:** fit **R51 and R52** (the two buttons). R47, R48, R49,
R50, and R53 stay **open**: their GPIOs are inputs or a 1-Wire bus here.
**host-dev:** leave the encoder jumpers P1 open (PA0/PA1 are inputs).

## CubeMX

`cmx/stm32.ioc` started as a copy of lab-test-01's. Fixed can-mcu parts as
there (80 MHz from HSE, LSE for the RTC, CAN1 250 kbit/s with ABOM, NART,
TXFP, USART1 VCP, I2C1, no EXTI and no watchdog in CubeMX). Device-specific:

| Pin | Mode |
| --- | --- |
| PA4, PA5, PA6 | ADC1 IN9, IN10, IN11 single-ended; IN5..IN8 removed |
| PA8, PA9, PA10 | TIM1 CH1..CH3 PWM generation, polarity high |
| PB12, PB2 | GPIO output open-drain, no pull, level high |
| PA0, PA1, PA3, PA7, PB4, PB5, PC13, PA15 | GPIO input, pull-up |
| PB10, PB11 | GPIO input, no pull (frontends) |
| PB13, PB14, PA2 | Unassigned |

The firmware configures the inputs, outputs, and 1-Wire pins itself as well
(`src/board.cpp`), so it does not depend on the GPIO labels.

## Firmware

| File | Content |
| --- | --- |
| `src/app.cpp` | Composition root: drivers, services, B1S, console, supervision |
| `src/board.hpp/.cpp` | Host wiring: buttons, digital inputs, outputs (TIM1), analog channels, 1-Wire pins |
| `src/io_console.hpp/.cpp` | Service command `io`: input levels, analog mV, output duty (read-only) |
| `src/device_settings.hpp/.cpp` | EEPROM layout and the boot count / address record |
| `src/dev_tools.hpp` | Seam to the development tools; `src/dev_tools_disabled.cpp` is the stand-in |
| `src/dev/dev_tools.cpp` | Development tool: `do <n> <percent>` switches an output by hand |

Shared can-mcu code (`../common/`): runtime, VCP log and console, button
frontends, I2C, CAN, McuMonitor, board temperatures, supervision, firmware
update support, and the health, I2C scan, and CAN monitor services.

- **B1S identity:** product `engine-mon` (code 3), preferred address 40,
  NMEA 2000 class 50 (propulsion), function 140 (engine room monitoring);
  model "Semantic Boat Engine Monitor".
- **Channels on B1S:** 0 enclosure (TMP75), 1 MCU die; as standard PGN their
  source would be "engine room". Default alarms: enclosure warning above
  65 °C and alarm above 75 °C (60 s average), MCU alarm above 80 °C,
  enclosure sensor fault a warning after 10 s.
- **Window pool:** 24 windows of 32 buckets (about 5.8 KiB of RAM): an
  engine monitor can spend its memory on averages and alarms.
- **EEPROM** (from the top): settings, input module calibration, health
  record (as on LT1), sensor calibration (32 slots), B1S configuration (16
  channel slots), alarm rules and outputs (16 channels); about 21 KiB.
- **Console** (VCP and over CAN): `help`, `health`, `sensors`, `sensor`,
  `ack`, `io`; with development tools also `do`.
- **Identify** over CAN blinks the yellow CAN LED fast.

Sizes (2026-10-20): Release 70.4 KB of flash, 15.2 KB RAM (static); Debug
142 KB.

## Build and flash

```sh
script/build.sh engine-mon debug                       # without development tools
cmake --preset engine-mon-debug -B build/engine-mon/dev -DSB_DEV_TOOLS=ON
cmake --build build/engine-mon/dev                     # with them (Debug only)
script/build.sh engine-mon release                     # product build
script/build.sh engine-mon debug flash-all             # bootloader + application (ST-Link)
python src/tools/can-bench/b1s_tool.py firmware <address> build/engine-mon/release/src/dev/can-mcu/engine-mon/engine-mon.bin
```

The application starts from the [can-mcu bootloader](../can-mcu-bootloader/README.md)
at 0x08004000; a fresh can-mcu needs `flash-all` once.

## Open points

- Engine host board: input conditioning, output drivers with pull-downs,
  connectors.
- Engine channels: rpm on DI0 (frequency input), coolant and exhaust
  temperatures (1-Wire or senders), oil pressure, alarm outputs.
- Bring-up on hardware.
