# host-dev

`host-dev` is a generic development and lab host board for the
[can-mcu](can-mcu.md). It breaks out the can-mcu host headers and provides
buttons, a status LED, a buzzer, I2C connectors, a rotary encoder, and a header
for the remote. Firmware: `lab-test-01` = can-mcu + host-dev.

Hardware source: `hardware/can-mcu/can-mcu.eprj2` (EasyEDA Pro), schematic and
PCB **host-dev**. Pin and net details were extracted from the host-dev PCB
netlist; designators refer to that design. Board outline 50 x 70 mm with
rounded corners, matching the can-mcu.

> As for the can-mcu, the netlist comes from the 2026-09-09 document snapshot
> in the `.eprj2` file. Values corrected by the designer since then (status
> LED resistors) follow the designer.

## Feature summary

- Sockets for the can-mcu, plus parallel pin headers to wire peripherals
  directly to every can-mcu host pin
- 12 V power passthrough with a 100 µF electrolytic capacitor
- 3.3 V and GND power headers (4 pins each)
- Four I2C connectors
- Six tactile push buttons on the can-mcu button frontends
- Green/red status LED on a header, to be wired to GPIOs
- Small buzzer with a low-side driver on a header, provision for an external
  3.3 V buzzer
- Bourns PEC09 rotary encoder with push button and RC frontend
- Header for the remote (separate PCB, not covered by `lab-test-01` for now)

Nothing on host-dev is connected to an MCU GPIO by default except the buttons
(through the can-mcu frontends) and the remote LED line (PA2, see below).
LED, buzzer, and encoder must be wired or jumpered explicitly.

## can-mcu connection

| Ref | Type | Purpose |
| --- | --- | --- |
| U3 | 1x20 female socket, 8.5 mm | Receives can-mcu J1 |
| U4 | 1x20 female socket, 8.5 mm | Receives can-mcu J2 |
| J1 | 1x20 pin header | Parallel to U3 (can-mcu J1) for peripheral wiring |
| J2 | 1x20 pin header | Parallel to U4 (can-mcu J2) for peripheral wiring |

J1/J2 carry exactly the can-mcu J1/J2 pinout; see
[can-mcu host headers](can-mcu.md#host-headers). The socket positions are
mirrored relative to the can-mcu headers because the can-mcu mounts
component-side-down.

Exception: pin 20 of U4/J2 (can-mcu +12V) is a separate net (`+12V_H`) on
host-dev. It is broken out on J2.20 only and is **not** connected to the
passthrough terminals or to C1.

## Power

| Ref | Type | Pins | Purpose |
| --- | --- | --- | --- |
| U1 | 5.08 mm 2-pin terminal | 1 GND, 2 +12V | 12 V passthrough |
| U2 | 5.08 mm 2-pin terminal | 1 GND, 2 +12V | 12 V passthrough |
| C1 | 100 µF / 50 V electrolytic | +12V to GND | Bulk capacitance on the passthrough |
| H4 | 1x4 pin header | +3V3 x4 | 3.3 V power header |
| H5 | 1x4 pin header | GND x4 | GND power header |

U1 and U2 are wired in parallel. The passthrough is independent of the can-mcu
power path and only connects to the stack through external wiring.

## I2C connectors

The headers H1, H2, H3, and H6 are four parallel 1x4 rows. Each column of the
block forms one 4-pin I2C connector, giving four connectors.

| Row | Net |
| --- | --- |
| H1 | +3V3 |
| H2 | GND |
| H3 | I2C_SCL |
| H6 | I2C_SDA |

The bus is the can-mcu I2C1 (PB8/PB9), shared with the on-board EEPROM (0x52)
and TMP75 (0x4F). The can-mcu already provides 2.2 kΩ pull-ups; host-dev has
none. Peripherals must not use addresses 0x52 or 0x4F.

## Buttons

| Button | Frontend | GPIO when bridged |
| --- | --- | --- |
| BF1 | BTN_FUN_0 | PC13 |
| BF2 | BTN_FUN_1 | PA15 |
| BF3 | BTN_FUN_2 | PB2 |
| BF4 | BTN_FUN_3 | PB4 |
| BF5 | BTN_FUN_4 | PB10 |
| BF6 | BTN_FUN_5 | PB11 |

Each button switches its `BTN_*` header pin to GND. Pull-up and debounce live
on the can-mcu frontend. A button only reaches its GPIO if the matching 0 Ω
bridge is fitted on the can-mcu; otherwise the frontend pin is unused and the
GPIO stays free (see [can-mcu button frontends](can-mcu.md#button-frontends)).
Each button is either used through the frontend or its GPIO is used for
something else, not both.

The seventh frontend, BTN_POWER (PB5), has no tactile button on host-dev. It is
used by the encoder push button and the remote.

## Status LED (H9, LED1)

LED1 is a bicolor red/green LED with a common anode on +3V3. The cathodes come
out on header H9 through series resistors:

| H9 pin | Series R | Color | Approx. current | Meaning |
| --- | --- | --- | --- | --- |
| 1 | 120 Ω (R1) | Red | ~8 mA | "Look here" (blinking) |
| 2 | 1.5 kΩ (R2) | Green | ~0.5 mA | "All good" (steady) |

- Wire each H9 pin to a GPIO output. **Active low**: GPIO low = LED on,
  GPIO high or high-Z = off. Push-pull or open-drain both work. For dimming,
  use a timer PWM pin with inverted output polarity.
- Nothing is connected by default.

## Buzzer (H7, BUZ1)

BUZ1 (QMB-09B-03) is a **passive electromagnetic** buzzer with a resonant
frequency of about 2.7 kHz. It has no oscillator of its own and must be driven
with a square wave, so H7.1 belongs on a timer PWM pin. It sits between +3V3
and the drain of Q21 (AO3400A N-channel MOSFET), with a 1N4148W freewheeling
diode.

| H7 pin | Net | Purpose |
| --- | --- | --- |
| 1 | Gate input (220 Ω series, 47 kΩ pull-down) | Wire to a GPIO |
| 2 | Q21 drain / buzzer low side | External buzzer negative |
| 3 | +3V3 | External buzzer positive |

- **Active high**: input high = coil energized. The pull-down keeps it silent
  while the pin is unconfigured.
- Drive H7.1 with PWM at the tone frequency; 50 % duty gives full volume and
  lower duty reduces loudness (as remote-a does with `ToneDriver`). Loudness
  peaks near the 2.7 kHz resonance.
- Never leave the input statically high: the coil would draw continuous
  current without making sound.
- An external buzzer connects between H7.3 (+) and H7.2 (-), in parallel with
  BUZ1 and switched by Q21. Only 3.3 V buzzers are supported.

## Rotary encoder (DEC1, P1)

DEC1 is a Bourns PEC09 incremental encoder with a push switch.

- A and B (`DEC_A`, `DEC_B`): 10 kΩ pull-ups, 10 nF to GND, ESD protection,
  then 1 kΩ + 10 nF RC filter towards the MCU. The common pin is on GND.
- **Jumper header P1** connects the filtered channels to the MCU:

  | Jumper | Connects | MCU |
  | --- | --- | --- |
  | P1.3 - P1.4 | Encoder A | PA0 (TIM2_CH1) |
  | P1.1 - P1.2 | Encoder B | PA1 (TIM2_CH2) |

  TIM2 CH1/CH2 allow hardware quadrature decoding (encoder mode). Without the
  jumpers PA0/PA1 are free.
- The push switch connects to `BTN_POWER` (switch to GND), so it uses the
  can-mcu BTN_POWER frontend and reaches **PB5** when R53 is fitted.
- Signals are active low (switch closed = GND).

## Remote header (H8)

Six-pin header for the remote (incremental encoder, two tactile push buttons,
one LED). Not used by `lab-test-01` yet.

| H8 pin | Net | Notes |
| --- | --- | --- |
| 1 | GND | |
| 2 | DEC_A | Shared with the on-board encoder |
| 3 | DEC_B | Shared with the on-board encoder |
| 4 | BTN_POWER | Shared with the encoder push switch |
| 5 | BTN_FUN_0 | Shared with BF1 |
| 6 | Remote LED | PA2 (TIM2_CH3) through 300 Ω, permanently connected |

- The remote and the on-board encoder share the same nets; fit only one
  encoder at a time.
- PA2 is hard-wired to H8.6 through R5. When the remote is not connected this
  only matters if PA2 is used for something else on J1.10.

## Shared resources summary

| Resource | Used by |
| --- | --- |
| PA0, PA1 | Encoder through P1 jumpers |
| PA2 | Remote LED (always connected through 300 Ω) |
| PB5 (BTN_POWER) | Encoder switch, remote |
| PC13 (BTN_FUN_0) | BF1, remote |
| PC13, PA15, PB2, PB4, PB10, PB11 | Buttons, when can-mcu bridges are fitted |
| I2C1 | I2C connectors, can-mcu EEPROM and TMP75 |
| H9, H7 | Free choice of GPIOs, wired by hand |
