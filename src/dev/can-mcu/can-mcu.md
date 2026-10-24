# can-mcu

`can-mcu` is a generic MCU board for marine CAN applications. It carries the
MCU, power supply, protection, CAN interface, and common support circuitry, so
that the host boards stacked on it (which implement the actual application)
stay simple. Firmware for a device always targets the pair *can-mcu + host*,
for example `lab-test-01` = can-mcu + [host-dev](host-dev.md).

Hardware source: `hardware/can-mcu/can-mcu.eprj2` (EasyEDA Pro), schematic and
PCB **main-board**, title block V1.0. Pin and net details in this document were
extracted from the main-board PCB netlist; designators refer to that design.

> The netlist was read from the plain document snapshot stored in the `.eprj2`
> file, which dates from 2026-09-09. Later edits live in EasyEDA's encrypted
> history and could not be read. Where the designer has since corrected
> something (for example the H6 CAN header), this document follows the
> designer. Re-check details against an exported netlist when in doubt.

## Feature summary

- STM32L431CCT6 (LQFP48, Cortex-M4F, 80 MHz, 256 KiB flash, 64 KiB SRAM)
- 8 MHz HSE crystal and 32.768 kHz LSE crystal
- SWD header (STDC14) with TRACESWO and ST-Link virtual COM port
- AT24C256C 256 kbit (32 KiB) I2C EEPROM
- TMP75 I2C temperature sensor for enclosure temperature
- 12 V to 3.3 V buck converter
- Reverse-polarity, transient, and basic EMI protection on the 12 V input
- RJ45 CAN connector with SN65HVD233 transceiver, no termination, two LEDs
- 12 V input header (8 A), power-switch header (8 A)
- High-side switched host-load output (8 A), controlled by the MCU
- Seven button frontends (opt-in through 1206 0 Ω bridges)
- ESD protection on every externally routed pin
- Backup-battery input for VBAT (RTC)
- Reset button
- Two 1x20 2.54 mm host headers, Arduino-like stacking
- Form factor 50 x 70 mm

## Mechanical and stacking

The board is 50 x 70 mm. The host headers J1 and J2 sit on the top side along
the two long edges. RJ1, the power terminals, the SWD header, the battery
connector, and the reset button are on the **bottom** side. The host boards
place the matching sockets at mirrored positions, so the can-mcu is mounted
component-side-down onto the host and its connectors face outward.

The RJ45 connector is about 13 mm high and dominates the sandwich height. The
4-pin header **H6** carries CAN data and power, so hosts can implement the CAN
connector differently (and build a flatter sandwich) and leave RJ1
unpopulated. H6 is not in the netlist snapshot this document was extracted
from, so its pinout is still to be documented.

## Power

### Connectors

| Ref | Type | Pin 1 | Pin 2 | Purpose |
| --- | --- | --- | --- | --- |
| CN2 | 5.08 mm 2-pin, right angle | GND | 12 V in | 12 V input, 8 A max |
| CN1 | 5.08 mm 2-pin, vertical | 12 V in (from CN2.2) | to protection | Power switch, 8 A; wire to a switch or bridge |
| CN3 | 5.08 mm 2-pin, vertical | GND | switched +12 V | Host load output, 8 A |
| CN5 | 5.08 mm 2-pin, right angle | VAUX- | VAUX+ | 12 V from the CAN backbone (RJ1) |
| CN6 | JST SH 2-pin | GND | battery + | VBAT backup battery |

### Power path

```text
RJ1 4/5 (VAUX+) ---> CN5.2      RJ1 7/8 (VAUX-) ---> CN5.1      (not connected to board GND)

CN2.2 --> CN1 (switch) --> D1 TVS --> Q1 reverse-polarity P-FET --> +12V
                                                                      |-- J2.20 (+12 V to host)
                                                                      |-- Q2 high-side P-FET --> CN3.2 (host load)
                                                                      `-- L1 ferrite --> U1 buck --> +3V3
```

- **CAN power is not routed to the PSU.** The backbone pair VAUX+/VAUX- only
  goes to CN5. To power the board from the backbone, loop CN5 to CN2 with a
  wire; alternatively connect any other 12 V source to CN2. This is deliberate:
  it prevents accidental ground loops when boards are configured differently.
  VAUX- is not connected to board GND on the can-mcu.
- **CN1 power switch** is in series between CN2.2 and the protection stage.
  Without a switch or a bridge the board is unpowered.
- **Protection:** D1 SMBJ20CA bidirectional TVS to GND, then Q1 (AGM60P90D
  P-channel MOSFET) as reverse-polarity protection with a 15 V Zener gate clamp
  (U2). C3 10 µF on +12V.
- **No fuse on the board.** Each backbone spur has its own ATO fuse; the
  backbone itself carries the transient protection and is specified for 2 A
  (Cat6).
- **+12V** (protected, after the switch) feeds the buck converter, the host-load
  switch, and J2.20 to the host.
- **PSU:** U1 EA8951T8R synchronous buck, 68 µH inductor, 47 µF output
  capacitor with a 330 mΩ series resistor, L1 BLM21 ferrite at the input,
  feedback divider 21 k / 12 k for 3.3 V.

### Host-load switch (HOST_POWER, PB15)

CN3 provides 12 V that the MCU can switch off completely, guaranteeing zero
leakage for loads such as fans or LEDs (battery-powered marine installations).

- PB15 (`HOST_POWER`) -> 4.7 kΩ -> Q3 NPN (10 kΩ base pull-down) -> Q2 gate.
- Q2 AGM60P90D P-channel MOSFET, source on +12V, drain to CN3.2.
- Gate network: 10 kΩ gate-source pull-up, 15 V Zener clamp (ZD1), 1 µF
  gate-source capacitor with 1 kΩ in series from Q3. This gives a soft
  turn-on and a turn-off in the order of 10 ms.
- D2 SS510B freewheeling diode across the output for inductive loads.
- **Active high.** PB15 high = load on. While the MCU is in reset or PB15 is
  floating, the base pull-down keeps the load **off**.

### Backup battery (CN6)

CN6 is the RTC backup-battery input. VBAT (MCU pin 1) is fed through a BAV70
diode-OR from +3V3 and from CN6.2, with 100 nF to GND. A backup cell on CN6
keeps the RTC, LSE, and backup registers running while the board is
unpowered. Without a cell VBAT follows +3V3 minus a diode drop, and the RTC
loses its time on power loss.

## CAN interface

### RJ1 pinout

The RJ45 uses all four twisted pairs (T568 pair numbering):

| RJ1 pin | Pair | Signal |
| --- | --- | --- |
| 1, 2 | 1 | CAN data pair (CAN_H/CAN_L) |
| 3, 6 | 2 | RC filter: 100 Ω (1206) + 100 nF to GND |
| 4, 5 | 3 | VAUX+ (backbone 12 V) -> CN5.2 |
| 7, 8 | 4 | VAUX- (backbone GND) -> CN5.1 |
| 9, 10 | LED | Yellow LED, MCU-controlled (`CAN_ACTIVE`) |
| 11, 12 | LED | Green LED, permanently on from +3V3 |

- The board is designed for spurs of a custom backbone. **There is no
  termination**; termination is done in the wider network.
- CAN line protection: PESD2CAN ESD diode, WCM3225 common-mode choke, 47 pF to
  GND on each line at the transceiver.
- The data pair and the RC pair share the board GND; the 12 V pair (VAUX) does
  not, see [Power path](#power-path).

### Transceiver

U5 SN65HVD233 (3.3 V). LBK (loopback) is tied to GND.

| MCU pin | Net | Transceiver pin |
| --- | --- | --- |
| PA12 | CAN_TX | D |
| PA11 | CAN_RX | R |
| PB1 | CAN_MUTE (100 Ω series, 33 kΩ to GND) | Rs (mode select) |

Rs mode behavior:

- PB1 driven **low**: high-speed mode.
- PB1 **floating** (reset, unconfigured): the 33 kΩ pull-down puts the
  transceiver in slope-control mode, so the bus is usable but with reduced
  edge rate.
- PB1 driven **high**: low-power standby, the driver is off (listen-only).
  Check the datasheet for receiver behavior before relying on it.

Firmware policy: drive PB1 **low** (high-speed mode) whenever CAN is active.
The bus runs NMEA 2000 at 250 kbit/s (sample point 87.5 %), well within the
transceiver's high-speed timing. Drive PB1 high to silence the transmitter
(standby, shutdown). Slope-control mode can be evaluated later as an EMI
measure.

### LEDs

- Green (RJ1 11/12): anode through 1 kΩ to +3V3, cathode to GND. Always on
  when 3.3 V is present.
- Yellow (RJ1 9/10): anode through 1 kΩ to +3V3, cathode through 100 Ω to PB0
  (`CAN_ACTIVE`). **Active low**: PB0 low = LED on. Intended as a CAN activity
  indicator but free for other uses.

## MCU support circuitry

- **Clocks:** X2 8 MHz crystal on PH0/PH1 (33 pF load caps), X1 32.768 kHz
  crystal on PC14/PC15 (22 pF).
- **Reset:** NRST via 220 Ω from the RESET net; RESET has 10 kΩ pull-up,
  100 nF to GND, the SW1 reset button, ESD protection, and goes to CN7.12.
- **BOOT0:** 47 kΩ pull-down, boots from flash.
- **Series resistors:** 33 Ω on SPI-like, debug, UART, and I2C lines; 100 Ω on
  the remaining host GPIOs. All header pins have ESD diodes.

### Debug header CN7 (STDC14, 1.27 mm 2x7)

| CN7 pin | Signal | MCU |
| --- | --- | --- |
| 3 | +3V3 (target VCC) | |
| 4 | SWDIO | PA13 |
| 5, 7 | GND | |
| 6 | SWCLK | PA14 |
| 8 | TRACESWO | PB3 |
| 12 | NRST | NRST |
| 13 | STL_COM_RX (VCP) | PB7, USART1_RX |
| 14 | STL_COM_TX (VCP) | PB6, USART1_TX |
| 1, 2, 9, 10, 11 | not connected | |

USART1 on PB6/PB7 is the ST-Link virtual COM port. On remote-a the VCP lines
were crossed and needed the USART swap feature. The can-mcu wiring was
corrected for this; the first can-mcu bring-up (lab-test-01) verifies it
without the swap.

## On-board I2C devices

I2C1 on PB8 (SCL) / PB9 (SDA), 2.2 kΩ pull-ups to +3V3 on the can-mcu. The bus
is also routed to the host (J2.1/J2.3); hosts must not add strong pull-ups.

| Device | Part | 7-bit address | Notes |
| --- | --- | --- | --- |
| EEPROM | AT24C256C | 0x52 | A2=0, A1=1, A0=0; WP tied to GND (always writable). Same address as remote-a. |
| Temperature | TMP75 | 0x4F | A2=A1=A0=1; ALERT not connected. Measures enclosure temperature. |

## Button frontends

Seven identical frontends condition buttons on the host. The host connects a
button between the `BTN_*` pin and GND.

```text
+3V3 --10k--+-- BTN_x (host header J2) --[button to GND on host]
            |
          100nF, ESD
            |
            +--1k--+--[R 0R 1206, opt-in]-- GPIO (also on host header J1)
                   |
                 100nF
```

- **Active low**, pull-up and RC debounce in hardware.
- The 0 Ω bridge (R47 to R53) connects the frontend to its GPIO. **The bridges
  must be opted in** because each frontend occupies a GPIO. Without the bridge
  the GPIO is free and available on J1 as a normal host GPIO.
- With the bridge fitted, the GPIO is driven by the frontend and must not be
  used for anything else on the host.

| Frontend | Header pin | 0 Ω bridge | GPIO | GPIO net | EXTI line |
| --- | --- | --- | --- | --- | --- |
| BTN_FUN_0 | J2.10 | R47 | PC13 | GPIO_PC13_F0 | 13 |
| BTN_FUN_1 | J2.11 | R48 | PA15 | GPIO_PA15_F1 | 15 |
| BTN_FUN_2 | J2.12 | R49 | PB2 | GPIO_PB2_F2 | 2 |
| BTN_FUN_3 | J2.13 | R50 | PB4 | GPIO_PB4_F3 | 4 |
| BTN_FUN_4 | J2.14 | R51 | PB10 | GPIO_PB10_F4 | 10 |
| BTN_FUN_5 | J2.15 | R52 | PB11 | GPIO_PB11_F5 | 11 |
| BTN_POWER | J2.16 | R53 | PB5 | GPIO_PB5_FP | 5 |

All seven use distinct EXTI lines. Interrupt vectors: EXTI2 (PB2), EXTI4 (PB4),
EXTI9_5 (PB5), EXTI15_10 (PC13, PA15, PB10, PB11). Any other GPIO used as an
interrupt source must avoid EXTI line numbers already used by fitted frontends.

## Host headers

Two 1x20 2.54 mm pin headers. GPIO net names encode the pin and its intended
role: `Fn` = button frontend n, `FP` = power-button frontend, `T11` to `T13` =
TIM1 CH1 to CH3, `T21` to `T24` = TIM2 CH1 to CH4, `D_*` = display (SPI EPD as
used by host-network-display).

### J1

| Pin | Net | MCU | Notes |
| --- | --- | --- | --- |
| 1 | +3V3 | | |
| 2 | GPIO_PA4_D_CS | PA4 | SPI1_NSS, ADC1_IN9 |
| 3 | GPIO_PA6_D_DC | PA6 | SPI1_MISO, ADC1_IN11 |
| 4 | GPIO_PA5_D_SCK | PA5 | SPI1_SCK, ADC1_IN10 |
| 5 | GND | | |
| 6 | GPIO_PA7_D_MOSI | PA7 | SPI1_MOSI, ADC1_IN12 |
| 7 | GPIO_PB12_D_BUSY | PB12 | SPI2_NSS |
| 8 | GPIO_PB13_D_PWR | PB13 | SPI2_SCK |
| 9 | GPIO_PB14_D_RST | PB14 | SPI2_MISO |
| 10 | GPIO_PA2_T23 | PA2 | TIM2_CH3, USART2_TX, ADC1_IN7 |
| 11 | GPIO_PA3_T24 | PA3 | TIM2_CH4, USART2_RX, ADC1_IN8 |
| 12 | GPIO_PC13_F0 | PC13 | BTN_FUN_0 frontend if bridged |
| 13 | GND | | |
| 14 | GPIO_PA15_F1 | PA15 | BTN_FUN_1 frontend if bridged |
| 15 | GPIO_PB2_F2 | PB2 | BTN_FUN_2 frontend if bridged |
| 16 | GPIO_PB4_F3 | PB4 | BTN_FUN_3 frontend if bridged |
| 17 | GPIO_PB10_F4 | PB10 | BTN_FUN_4 frontend if bridged; USART3_TX, I2C2_SCL |
| 18 | GPIO_PB11_F5 | PB11 | BTN_FUN_5 frontend if bridged; USART3_RX, I2C2_SDA |
| 19 | GPIO_PB5_FP | PB5 | BTN_POWER frontend if bridged |
| 20 | GND | | |

### J2

| Pin | Net | MCU | Notes |
| --- | --- | --- | --- |
| 1 | I2C_SCL | PB8 | I2C1, shared with EEPROM and TMP75 |
| 2 | GND | | |
| 3 | I2C_SDA | PB9 | I2C1, shared with EEPROM and TMP75 |
| 4 | GPIO_PA8_T11 | PA8 | TIM1_CH1 |
| 5 | GPIO_PA9_T12 | PA9 | TIM1_CH2 |
| 6 | GPIO_PA10_T13 | PA10 | TIM1_CH3 |
| 7 | GPIO_PA0_T21 | PA0 | TIM2_CH1, ADC1_IN5 |
| 8 | GND | | |
| 9 | GPIO_PA1_T22 | PA1 | TIM2_CH2, ADC1_IN6 |
| 10 | BTN_FUN_0 | | Frontend input, active low |
| 11 | BTN_FUN_1 | | Frontend input, active low |
| 12 | BTN_FUN_2 | | Frontend input, active low |
| 13 | BTN_FUN_3 | | Frontend input, active low |
| 14 | BTN_FUN_4 | | Frontend input, active low |
| 15 | BTN_FUN_5 | | Frontend input, active low |
| 16 | BTN_POWER | | Frontend input, active low |
| 17 | GND | | |
| 18 | not connected | | |
| 19 | not connected | | |
| 20 | +12V | | Protected and switched 12 V (after CN1 and Q1) |

The alternate functions listed are a starting point; confirm them in CubeMX
when assigning peripherals.

## MCU pin map

| Pin | Port | Net / function | Destination |
| --- | --- | --- | --- |
| 1 | VBAT | VBAT | BAV70 diode-OR of +3V3 and CN6 |
| 2 | PC13 | GPIO_PC13_F0 | J1.12, BTN_FUN_0 frontend |
| 3, 4 | PC14, PC15 | LSE | X1 32.768 kHz |
| 5, 6 | PH0, PH1 | HSE | X2 8 MHz |
| 7 | NRST | RESET | SW1, CN7.12 |
| 10 to 13 | PA0 to PA3 | GPIO_PA0_T21 to GPIO_PA3_T24 | J2.7, J2.9, J1.10, J1.11 |
| 14 to 17 | PA4 to PA7 | D_CS, D_SCK, D_DC, D_MOSI | J1.2, J1.4, J1.3, J1.6 |
| 18 | PB0 | CAN_ACTIVE | Yellow RJ45 LED, active low |
| 19 | PB1 | CAN_MUTE | Transceiver Rs |
| 20 | PB2 | GPIO_PB2_F2 | J1.15, BTN_FUN_2 frontend |
| 21, 22 | PB10, PB11 | GPIO_PB10_F4, GPIO_PB11_F5 | J1.17, J1.18, frontends 4 and 5 |
| 25 to 27 | PB12 to PB14 | D_BUSY, D_PWR, D_RST | J1.7, J1.8, J1.9 |
| 28 | PB15 | HOST_POWER | Host-load switch, active high |
| 29 to 31 | PA8 to PA10 | GPIO_PA8_T11 to GPIO_PA10_T13 | J2.4, J2.5, J2.6 |
| 32, 33 | PA11, PA12 | CAN_RX, CAN_TX | SN65HVD233 |
| 34 | PA13 | SWDIO | CN7.4 |
| 37 | PA14 | SWCLK | CN7.6 |
| 38 | PA15 | GPIO_PA15_F1 | J1.14, BTN_FUN_1 frontend |
| 39 | PB3 | TRACESWO | CN7.8 |
| 40 | PB4 | GPIO_PB4_F3 | J1.16, BTN_FUN_3 frontend |
| 41 | PB5 | GPIO_PB5_FP | J1.19, BTN_POWER frontend |
| 42, 43 | PB6, PB7 | USART1 TX, RX | CN7.14, CN7.13 (ST-Link VCP) |
| 44 | BOOT0 | | 47 kΩ to GND |
| 45, 46 | PB8, PB9 | I2C1 SCL, SDA | EEPROM, TMP75, J2.1, J2.3 |

## Firmware notes

- Signal polarities: buttons active low, yellow CAN LED active low, HOST_POWER
  active high, CAN_MUTE active high (low = high-speed mode).
- Defaults while the MCU is in reset: host load off, yellow LED off,
  transceiver in slope-control mode. Firmware switches the transceiver to
  high-speed mode (PB1 low) when it starts CAN.
- PC13 is in the backup domain and has limited drive strength; it is fine as a
  button input but should not drive loads.
- PB4 (NJTRST) and PA15 (JTDI) are free for GPIO use because only SWD is used;
  keep PA13, PA14, and PB3 reserved for debugging.
- CubeMX starting points: `hardware/can-mcu/stm32/stm32.ioc` (labels for the
  display host), `src/dev/can-mcu/lab-test-01/cmx/stm32.ioc`, and
  `src/dev/can-mcu/engine-mon/cmx/stm32.ioc`.

### Shared board support (`common/`)

Code that binds hardware fixed on the can-mcu lives in
[`common/`](common/) and is compiled into every can-mcu firmware through
`common/can-mcu.cmake` (`can_mcu_add_common(<target>)`). It is HAL code and
therefore deliberately outside `slstm32`. It currently provides:

- the slstm32 `Runtime` (HAL tick, nesting critical sections) and a DWT-based
  `delayMicroseconds()`;
- the ST-Link VCP log output (USART1, polled, non-blocking);
- the seven button frontends as interrupt-driven `ButtonConfig`s;
- `HalI2cBus` (`i2cBus()`): the slstm32 `I2cBus` on I2C1, with bus recovery
  (clock out a stuck slave, STOP, re-initialize) on any fault other than a
  NACK, so sensors can be hot-plugged;
- `canBusHardware()` / `attachCanDriver()`: the slstm32 `CanBusHardware` for
  bxCAN with an accept-all filter, receive/transmit/overrun interrupts, bus
  state from the error register, and transceiver mode control (PB1);
  `canActivityLed()` for the yellow RJ45 LED;
- `McuMonitor`: supply voltage (VDDA via VREFINT, about 23,000 samples per
  second with min/max windows) and MCU die temperature, both from factory
  calibration. It currently owns ADC1;
- all EXTI interrupt vectors, forwarded to `HAL_GPIO_EXTI_Callback()`;
- reset-cause capture and a board-info log;
- supervision: independent watchdog (`startWatchdog()`, frozen while a
  debugger halts the core), power monitor counting every VDD drop below a
  threshold (`startPowerMonitor()`, PVD interrupt), and a crash record in the
  RTC backup registers (`takeCrashRecord()`) with the fault status registers;
- `can_mcu_fatal_error()` for `Error_Handler` and `HardFault_Handler`: it
  stores the crash record, writes the reason to the VCP (if USART1 is
  running), and blinks the yellow CAN LED until the watchdog (if started)
  resets the MCU.
- services every can-mcu device can use: `HealthService` (health record in
  EEPROM, console `health`), `I2cScanService` (logs I2C devices as they
  appear and disappear), `CanMonitorService` (CAN state, traffic on the
  yellow LED, message log, `identify()` blinking).

Requirements on a device's CubeMX project: USART1 on PB6/PB7, and no EXTI
interrupts enabled in CubeMX (the shared layer owns those vectors).

## Open points

- Document the H6 CAN data and power header pinout.
- Verify the ST-Link VCP RX/TX direction on CN7 during the lab-test-01
  bring-up.
