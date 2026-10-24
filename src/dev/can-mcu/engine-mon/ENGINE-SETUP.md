# engine-mon: engine setup

**Status: agreed (2026-10-24), CubeMX and firmware to follow.** The
sensors, inputs, and outputs of the engine monitor, with their pins, the
drivers that read them, and the CubeMX configuration. The current firmware
still has the bring-up pin map of the [README](README.md#host-io); it
follows this document once the CubeMX project is regenerated.

Decisions (2026-10-24): D23 on PC13; 10 kΩ pull-downs on PA8, PA9, PA10;
all PT100 probes behind DFRobot SEN0198 amplifiers; I2C sensor cables as
short as possible, shielded, shield to engine ground; oil pressure and
coolant temperature from the engine's own senders for now. **Engine
control (preheat, start, stop) is a separate sub-project:** the outputs are
experimental, behind an external arming switch; the stop solenoid is
energize-to-stop. This firmware monitors only.

## Overview

```text
                      can-mcu (engine-mon)
 1-Wire  PB12 ── CT1780 thermocouple .............. dry exhaust
         PB13 ── DS18B20 .......................... gearbox oil
         PB2  ── DS18B20 .......................... calorifier water
 ADC     PA4..PA6 ── 3x PT100 amplifier ........... wet exhaust, alternator, heat exchanger
 Outputs PA8..PA10 ── MOSFET switches -> relays ... preheater, starter, stop solenoid
 Inputs  PA0, PA1, PA3, PA7 ── module AD1 D10..D13  rpm, ignition, starter solenoid, fuel stop solenoid
         PB4, PB5, PB14, PC13 ─ module AD2 D20..D23 gearbox reverse/forward, preheater relay, reserve
 LEDs    PA15 green, PA2 red (enclosure)
 Buttons PB10, PB11 (frontends BTN_FUN_4/5, reachable from outside)
 I2C1    PB8/PB9 ── AD1 (ADS1115 0x48): A10..A13  oil pressure, coolant temperature, alternator, starter voltage
                 ── AD2 (ADS1115 0x49): A20..A23  12 V system, reserve x3
                 ── NPI-19 (0x28) ................. raw water pressure 0..2.07 bar
                 ── DHT20 (0x38) .................. engine room temperature and humidity
                 ── on the can-mcu: EEPROM 0x52, TMP75 0x4F (enclosure)
```

AD1 and AD2 are [12v-ad-input-iso](../../../lib/semantic-modules/12v-ad-input-iso.md)
modules: four isolated 12 V analog inputs (ADS1115) and four isolated
digital inputs each. The module's digital outputs (H1 OUT_D_0..3) go to the
MCU pins listed as D10..D13 and D20..D23.

## Signals

Channel numbers are a proposal for B1S (0 and 1 exist already).

### Temperatures

| Signal | Sensor | Interface | Pin / input | Driver | B1S channel |
| --- | --- | --- | --- | --- | --- |
| Dry exhaust | CT1780 (MAX31850K), K-type thermocouple | 1-Wire bus 0 | PB12 | `Ct1780Driver` | 3 |
| Wet exhaust | PT100, 350 °C probe, analog amplifier | ADC1 IN9 | PA4 | `AnalogSensor` + `rtdBridgeConversion` | 4 |
| Alternator | PT100, 350 °C probe, analog amplifier | ADC1 IN10 | PA5 | as above | 5 |
| Heat exchanger | PT100, 350 °C probe, analog amplifier | ADC1 IN11 | PA6 | as above | 6 |
| Gearbox oil | DS18B20 (to 125 °C) | 1-Wire bus 1 | PB13 | `Ds18b20Driver` | 7 |
| Calorifier water | DS18B20 (to 125 °C) | 1-Wire bus 2 | **PB2** (proposed) | `Ds18b20Driver` | 8 |
| Coolant (engine sender) | engine's sender | AD1 A11 | ADS1115 0x48 ch 1 | `AdInputIso12v` + conversion | 10 |
| Engine room | DHT20 | I2C 0x38 | | `Dht20Driver` | 14 (temperature), 15 (humidity) |
| Enclosure, MCU die | TMP75, internal | I2C 0x4F, ADC | | `BoardTemperatures` | 0, 1 (exist) |

### Pressures and voltages

| Signal | Sensor | Pin / input | B1S channel |
| --- | --- | --- | --- |
| Oil pressure | engine's sender | AD1 A10 | 9 |
| Alternator output voltage | direct | AD1 A12 | 11 |
| Starter voltage | direct | AD1 A13 | 12 |
| 12 V system | direct | AD2 A20 | 13 |
| Reserve | | AD2 A21, A22, A23 | later |
| Raw water pressure, 0..2.07 bar | Amphenol NovaSensor NPI-19, I2C, 30 psi gauge | I2C 0x28 | 16 |

The modules measure 0..18.8 V at their terminals (12 V systems; not 24 V).

**Raw water pressure.** Measured between the raw water pump and the engine
block: below about 1 bar is normal, above about 1.5 bar points to a blockage
(thresholds to be found by testing). Sensor: Amphenol NovaSensor NPI-19
with I2C output (datasheet AAS-920-699), 30 psi (207 kPa) gauge, ordering
code `NPI-19x-030G2` (x = port: A none, J 1/8 NPT, M M12x1.5):

- I2C address 0x28, 100..400 kHz (I2C1 runs at 100 kHz), no clock
  stretching; supply 2.5..5.5 V, 3.3 V on the I2C connector, about 3 mA.
- 14-bit pressure: 1638 counts at 0 bar, 14746 at 2.07 bar (10..90 % of the
  range), so about 0.16 mbar per count. Total error band ±1 % FSO (about
  ±20 mbar) over the compensated range.
- Measurement: command 0xAC (full measurement), wait until the status says
  "not busy", then read status, two bytes of bridge (pressure) data, and two
  bytes of temperature data. Valid data 8.4 ms after power-on. The datasheet
  gives no formula for the temperature bytes, so only the pressure is used.
- Operating temperature -40..125 °C (ranges from 100 kPa); wetted parts
  316L stainless steel.
- Needs a new slstm32 driver (`Npi19Driver`): about 100 lines, like the
  DHT20 driver (command, poll, read, pressure channel).

### Digital inputs (12 V, through the modules)

| Input | Signal | MCU pin | EXTI | Notes |
| --- | --- | --- | --- | --- |
| D10 | RPM (alternator W) | PA0 | 0 | Frequency input; verified at 750 Hz through the module on LT1. Pulses per revolution = pole pairs x pulley ratio, calibrate against a tachometer |
| D11 | Ignition | PA1 | 1 | |
| D12 | Starter solenoid | PA3 | 3 | Feedback: 12 V on the solenoid |
| D13 | Fuel stop solenoid | PA7 | 7 | Feedback |
| D20 | Gearbox reverse | PB4 | 4 | Bridge R50 open |
| D21 | Gearbox forward | PB5 | 5 | Bridge R53 open |
| D22 | Preheater relay | PB14 | 14 | Feedback of output DO0 |
| D23 | Reserve 1 | **PC13** (moved, see 1. below) | 13 | Bridge R47 open |

Module outputs are low while 12 V is present (the firmware inverts); an
unplugged module reads "no 12 V". Every input has its own EXTI line, so any
of them can count pulses.

### Outputs (MOSFET switch -> relay)

| Output | Load | MCU pin | Timer |
| --- | --- | --- | --- |
| DO0 | Preheater relay (glow plugs) | PA8 | TIM1_CH1 |
| DO1 | Starter relay | PA9 | TIM1_CH2 |
| DO2 | Stop solenoid relay | PA10 | TIM1_CH3 |

### Indicators and buttons

| Function | MCU pin | Notes |
| --- | --- | --- |
| Green LED (enclosure) | **PA15** (proposed) | TIM2_CH1: dimmable. Bridge R48 open |
| Red LED (enclosure) | **PA2** (proposed) | TIM2_CH3: dimmable; the same timer as green. On host-dev also wired to the remote LED (300 Ω), harmless |
| Buttons BTN0, BTN1 | PB10, PB11 | Frontends BTN_FUN_4/5, bridges R51, R52 fitted (as now) |

## MCU pin map (all 21 host GPIOs)

| Pin | Use | | Pin | Use |
| --- | --- | --- | --- | --- |
| PA0 | D10 rpm | | PB2 | 1-Wire 2 calorifier |
| PA1 | D11 ignition | | PB4 | D20 gearbox reverse |
| PA2 | Red LED | | PB5 | D21 gearbox forward |
| PA3 | D12 starter solenoid | | PB10 | Button 0 |
| PA4 | PT100 wet exhaust | | PB11 | Button 1 |
| PA5 | PT100 alternator | | PB12 | 1-Wire 0 CT1780 |
| PA6 | PT100 heat exchanger | | PB13 | 1-Wire 1 gearbox oil |
| PA7 | D13 fuel stop solenoid | | PB14 | D22 preheater relay |
| PA8 | DO0 preheater | | PC13 | D23 reserve |
| PA9 | DO1 starter | | | |
| PA10 | DO2 stop solenoid | | | |
| PA15 | Green LED | | | |

Fixed on the can-mcu and not available: PB8/PB9 (I2C1), PB15 (HOST_POWER),
PA11/PA12 (CAN), PB0/PB1 (CAN LED, transceiver mode), PB6/PB7 (VCP),
PA13/PA14/PB3 (debug). **No pin is left spare.**

## Sanity check

Resolved (2026-10-24):

1. **PB15 cannot be D23** (it is `HOST_POWER` on the can-mcu): D23 is on
   PC13.
2. **One sensor per 1-Wire bus** (`Ds18b20Driver`, `Ct1780Driver`): three
   buses, PB12, PB13, PB2. Sharing a bus would need a multi-drop driver
   (search ROM); that would free pins later.
3. **I2C2 is gone** (PB13/PB14 now used): every I2C device shares I2C1 with
   the EEPROM. Cables short and shielded, shield to engine ground.
4. **Outputs at reset:** 10 kΩ pull-downs on PA8, PA9, PA10.
5. **Engine control** (stop solenoid energize-to-stop, starter, preheater):
   a separate sub-project, outputs experimental behind an external arming
   switch. Interlocks (neutral only, crank time limits, not while running,
   preheat time limit, off on any fault) and who may command them over CAN
   (B1S has no write protection yet) are decided there.
6. **PT100:** SEN0198 amplifiers, about 30..350 °C (a cold engine reads at
   the low end, fine for alarms). Each analog input needs a **pull-down** on
   the host, so a missing probe becomes a fault instead of a floating
   184 °C (seen on LT1).
7. **Engine senders (A10, A11):** used as they are, calibrated in place;
   a dedicated sensor maybe later.
8. **Raw water pressure:** NPI-19 with I2C output at 0x28 (see above).

Still to check:

9. **I2C addresses** in use: 0x28 NPI-19, 0x38 DHT20, 0x48 AD1, 0x49 AD2,
   0x4F TMP75, 0x52 EEPROM: no conflict. 0x4A and 0x4B remain for two more
   input modules.
10. **Isolated ground:** the four analog inputs of AD1 (and of AD2) share the
    module's isolated ground GND_I; connect it to the engine block / battery
    negative where the senders are grounded.
11. **DHT20 in the engine room:** specified to 80 °C; oil vapour wears the
    humidity sensor over time.
12. **NPI-19 on its cable:** the sensor sits at the raw water pump, so its
    I2C cable is the longest one; it carries 3.3 V, GND, SCL, SDA (shielded,
    shield to engine ground, as decided).

## CubeMX configuration

The complete configuration of `cmx/stm32.ioc`, so that every can-mcu device
is set up the same way. Labels become `<LABEL>_Pin` / `<LABEL>_GPIO_Port`
in `main.h`; they are upper case, `<FUNCTION><n>_<SIGNAL>`.

### Pins

| Pin | Mode (CubeMX signal) | Pull | Initial level | Speed | Label |
| --- | --- | --- | --- | --- | --- |
| PA0 | GPIO_Input | Pull-up | | | `DI10_RPM` |
| PA1 | GPIO_Input | Pull-up | | | `DI11_IGNITION` |
| PA2 | TIM2_CH3 (PWM Generation CH3) | | | Low | `LED_RED` |
| PA3 | GPIO_Input | Pull-up | | | `DI12_STARTER_SOLENOID` |
| PA4 | ADC1_IN9 (single-ended) | | | | `AI0_WET_EXHAUST` |
| PA5 | ADC1_IN10 (single-ended) | | | | `AI1_ALTERNATOR` |
| PA6 | ADC1_IN11 (single-ended) | | | | `AI2_HEAT_EXCHANGER` |
| PA7 | GPIO_Input | Pull-up | | | `DI13_FUEL_STOP_SOLENOID` |
| PA8 | TIM1_CH1 (PWM Generation CH1) | | | Low | `DO0_PREHEATER` |
| PA9 | TIM1_CH2 (PWM Generation CH2) | | | Low | `DO1_STARTER` |
| PA10 | TIM1_CH3 (PWM Generation CH3) | | | Low | `DO2_STOP_SOLENOID` |
| PA11 | CAN1_RX | | | | `CAN_RX` |
| PA12 | CAN1_TX | | | | `CAN_TX` |
| PA13 | SYS_JTMS-SWDIO | | | | `SWDIO` |
| PA14 | SYS_JTCK-SWCLK | | | | `SWCLK` |
| PA15 | TIM2_CH1 (PWM Generation CH1) | | | Low | `LED_GREEN` |
| PB0 | GPIO_Output, push-pull | No pull | High (LED off) | Low | `CAN_ACTIVE` |
| PB1 | GPIO_Output, push-pull | No pull | Low (high-speed mode) | Low | `CAN_MUTE` |
| PB2 | GPIO_Output, open-drain | No pull | High (released) | Low | `OW2_CALORIFIER` |
| PB3 | SYS_JTDO-SWO | | | | `TRACESWO` |
| PB4 | GPIO_Input | Pull-up | | | `DI20_GEAR_REVERSE` |
| PB5 | GPIO_Input | Pull-up | | | `DI21_GEAR_FORWARD` |
| PB6 | USART1_TX | | | | `STL_COM_TX` |
| PB7 | USART1_RX | | | | `STL_COM_RX` |
| PB8 | I2C1_SCL | | | | `I2C_SCL` |
| PB9 | I2C1_SDA | | | | `I2C_SDA` |
| PB10 | GPIO_Input | No pull (frontend) | | | `BTN0` |
| PB11 | GPIO_Input | No pull (frontend) | | | `BTN1` |
| PB12 | GPIO_Output, open-drain | No pull | High (released) | Low | `OW0_DRY_EXHAUST` |
| PB13 | GPIO_Output, open-drain | No pull | High (released) | Low | `OW1_GEARBOX_OIL` |
| PB14 | GPIO_Input | Pull-up | | | `DI22_PREHEATER_RELAY` |
| PB15 | GPIO_Output, push-pull | No pull | Low (load off) | Low | `HOST_POWER` |
| PC13 | GPIO_Input | Pull-up | | | `DI23_RESERVE1` |
| PC14, PC15 | RCC_OSC32_IN/OUT (LSE crystal) | | | | |
| PH0, PH1 | RCC_OSC_IN/OUT (HSE crystal) | | | | |
| PH3 | BOOT0 (GPIO_Input) | | | | `BOOT0` |

Notes:

- **TIM2_CH1 on PA15:** CubeMX offers TIM2_CH1 on PA0 first; assign it on
  PA15 (click PA15, choose TIM2_CH1), PA0 stays a GPIO input.
- **No EXTI mode** on any input: the shared can-mcu code switches the
  buttons (and later the rpm input) to interrupts itself and owns all EXTI
  vectors.
- The firmware also configures the 1-Wire pins and the timer channels
  itself; CubeMX only gives them a safe start.

### Peripherals

| Peripheral | Settings |
| --- | --- |
| RCC | HSE: crystal (8 MHz); LSE: crystal (32.768 kHz). PLL source HSE, M /1, N x20, R /2: SYSCLK 80 MHz, HCLK, APB1, APB2 80 MHz. PLLSAI1 for the ADC: 32 MHz |
| SYS | Debug: Trace Asynchronous Sw (SWD + SWO on PB3); timebase SysTick |
| RTC | Activate clock source; RTC clock from LSE |
| CAN1 | Prescaler 20, BS1 13 TQ, BS2 2 TQ, SJW 1 TQ (250 kbit/s, sample point 87.5 %); ABOM enable, NART enable, TXFP enable, other options disabled; NVIC: CAN1 TX, RX0, SCE enabled |
| I2C1 | Standard mode, 100 kHz (timing `0x10D19CE4`); no interrupts, no DMA |
| USART1 | Asynchronous, 115200 baud, 8N1, no flow control; **no** USART1 interrupt (the shared can-mcu code owns it) |
| ADC1 | IN9, IN10, IN11 single-ended; Vrefint channel enabled; clock asynchronous /1 (PLLSAI1); 12 bit, right aligned, scan disabled, continuous disabled, 1 regular conversion (rank 1: Vrefint, 2.5 cycles; the firmware switches channels itself); no interrupt, no DMA |
| TIM1 | Clock internal; CH1, CH2, CH3 PWM Generation: mode PWM1, pulse 0, output polarity **High**, fast mode disabled, idle state Reset; prescaler and period don't matter (the firmware sets 1 kHz); no interrupts |
| TIM2 | Clock internal; CH1 (PA15) and CH3 (PA2) PWM Generation: mode PWM1, pulse 0, output polarity **High** for LEDs from the pin to GND (**Low** for LEDs from +3.3 V to the pin); no interrupts |
| NVIC | Priority group 4; only the CAN1 interrupts enabled (no EXTI, no USART1) |
| Project | Toolchain CMake, project name `stm32`, location `src/dev/can-mcu/engine-mon/cmx` |

After regenerating: commit, then pull the firmware changes (the
`USER CODE` hooks in `main.c` and `stm32l4xx_it.c` stay as they are).

## Follow-up work

- CubeMX: as in [CubeMX configuration](#cubemx-configuration).
- Firmware: the channels above, a new `Npi19Driver` (slstm32) for the raw
  water pressure, LEDs on TIM2; B1S channel slots and the measurement
  channel area enlarged from 16 to 24 (EEPROM still fits: about 30 of 32
  KiB); the window pool reviewed for about 18 channels.
- B1S: a representation for the digital inputs (switch states). There is no
  such quantity yet; candidates are standard PGN 127501 (Binary Switch Bank
  Status, `display`) or a new B1S message. To be decided ("Adding to B1S").
- B1S: engine data could also go out as standard PGNs (127488 engine
  speed, 127489 engine dynamic parameters): to decide per signal.
