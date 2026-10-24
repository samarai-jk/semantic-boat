# engine-mon: engine setup

**Status: draft for review (2026-10-24).** The sensors, inputs, and outputs
of the engine monitor as planned, with their pins, the drivers that read
them, and the points still to resolve. Nothing here is wired or implemented
yet; the current firmware still has the bring-up pin map of the
[README](README.md#host-io). Once this is agreed, firmware, CubeMX project,
and README follow it.

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
                 ── DHT20 (0x38) .................. engine room temperature and humidity
                 ── raw water pressure 0..2 bar ... part and address to be chosen
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
| Raw water pressure, 0..2 bar | I2C sensor, part to be chosen | I2C | 16 |

The modules measure 0..18.8 V at their terminals (12 V systems; not 24 V).

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

### Must change

1. **PB15 cannot be D23.** PB15 is `HOST_POWER` on the can-mcu: it drives
   the 8 A high-side switch on CN3 and is not on the host header. D23 moves
   to PC13 (input only; PC13 must not drive loads, which an input does not).
2. **One sensor per 1-Wire bus.** `Ds18b20Driver` and `Ct1780Driver` each
   handle one device per bus, so the second DS18B20 needs its own pin (PB2
   above). Sharing a bus would need a multi-drop driver (search ROM); that
   would free pins later.
3. **I2C2 is gone.** PB13/PB14 were kept for a second I2C bus; they are now
   a 1-Wire bus and D22. Every I2C sensor shares I2C1 with the can-mcu's
   EEPROM (see 9.).

### Safety (engine control): to decide before the outputs are wired

4. **Outputs at reset.** The TIM1 pins float until the firmware starts the
   timer. The MOSFET switch inputs need pull-downs, or a load switches during
   reset or while flashing. Check that the MOSFET modules have them.
5. **Stop solenoid type.** Is it energize-to-stop (12 V stops the engine) or
   energize-to-run (12 V lets it run)? With energize-to-run, a device reset
   or crash stops the engine; then the device must not switch it at all.
6. **Starter interlocks in the firmware:** crank only in neutral (D20 and
   D21 both off), not while running (rpm above a threshold), at most a few
   seconds per attempt with a pause between attempts, off at any fault or
   watchdog reset. Preheater with a maximum on-time (glow plugs burn out).
7. **Who may start the engine.** B1S has no write protection yet (decided
   2026-10-18: anyone on B1S may configure). Remote start over CAN needs at
   least an explicit, local confirmation, or starting stays on the device's
   buttons and the engine's own panel. Proposal: the first firmware only
   *monitors*; the outputs come with their interlocks in a separate step.

### Check

8. **PT100 inputs.** A PT100 cannot go to the ADC directly; it needs an
   amplifier. Assumed: DFRobot SEN0198 (driver and bridge formula verified on
   LT1). It covers about 30..350 °C: a cold engine reads at the low end, fine
   for alarms. Each analog input needs a **pull-down** on the host, so a
   missing probe becomes a fault instead of a floating 184 °C (seen on LT1).
9. **I2C in the engine room.** DHT20 and the raw water sensor on cables in
   the engine room put long wires and noise on I2C1, which also carries the
   EEPROM. Keep those cables short (well under 1 m), or put the sensors
   behind an I2C buffer or isolator. The two input modules are isolated
   (ISO1541) and are not the problem.
10. **I2C addresses.** In use: 0x38 DHT20, 0x48 AD1 (R33), 0x49 AD2 (R34),
    0x4F TMP75, 0x52 EEPROM. The raw water pressure sensor must not use one
    of them; part to be chosen.
11. **Raw water pressure.** Measured where? Before the raw water pump the
    pressure is often slightly below ambient (suction); after it, positive.
    A gauge sensor of 0..2 bar suits the pump outlet.
12. **Engine senders (A10 oil pressure, A11 coolant).** If they are the
    engine's resistive senders that also drive gauges, the module measures
    the voltage the gauge produces at the sender: usable, but calibrated in
    place (two points). Without a gauge a sender needs a pull-up on the host.
13. **Isolated ground.** All four analog inputs of a module share the
    module's isolated ground GND_I; connect it to the engine block / battery
    negative where the senders are grounded.
14. **DHT20 in the engine room.** Specified to 80 °C; oil vapour wears the
    humidity sensor over time.

## Follow-up work (after review)

- CubeMX: PB13 output open-drain (1-Wire), PB14 and PC13 inputs with
  pull-up, PA15 TIM2_CH1 and PA2 TIM2_CH3 PWM (LEDs); PA15 no longer an
  input.
- Firmware: the channels above; B1S channel slots and the measurement
  channel area enlarged from 16 to 24 (EEPROM still fits: about 30 of 32
  KiB); the window pool reviewed for about 18 channels.
- B1S: a representation for the digital inputs (switch states). There is no
  such quantity yet; candidates are standard PGN 127501 (Binary Switch Bank
  Status, `display`) or a new B1S message. To be decided ("Adding to B1S").
- B1S: engine data could also go out as standard PGNs (127488 engine
  speed, 127489 engine dynamic parameters): to decide per signal.
