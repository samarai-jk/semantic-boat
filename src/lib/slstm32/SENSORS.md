# Sensor drivers: bench findings

Each sensor driver in `slstm32/drivers/` was brought up on lab-test-01
(can-mcu + host-dev, 3.3 V, I2C at 100 kHz, 1-Wire bit-banged on a GPIO) with
a DFRobot module. This file records what was measured and decided, so a
device can use a sensor without repeating the bench work. Measurement
channels, alarms, and settings storage are described in the
[README](README.md#measurements-and-alarms).

| Sensor (module) | Driver | Interface | Status |
| --- | --- | --- | --- |
| DHT20 / AHT20 (DFRobot DHT20) | `Dht20Driver` | I2C 0x38 | Verified |
| SHT30/31/35 (DFRobot SHT31 outdoor) | `Sht31Driver` | I2C 0x44 (0x45) | Verified, heater verified |
| DS18B20 (DFRobot waterproof kit) | `Ds18b20Driver` | 1-Wire | Verified |
| CT1780 / MAX31850K (DFRobot SEN0656) | `Ct1780Driver` | 1-Wire | Verified (one of two modules dead) |
| PT100 amplifier (DFRobot SEN0198) | `AnalogSensor` + `sensors::rtdBridgeConversion` | ADC | Verified, not calibrated |
| VEML7700 (DFRobot SEN0228) | `Veml7700Driver` | I2C 0x10 | Verified at indoor light |
| AS3935 (DFRobot SEN0290) | `As3935Driver` | I2C 0x03 + IRQ | Verified incl. IRQ and antenna tuning; no real lightning seen |

## Common behaviour

- Drivers never block the main loop for longer than a bus transfer (except
  the documented one-time initializations), never fail `init()` because a
  sensor is missing, and reconnect when a sensor is plugged in again.
- Drivers push results into `sensors::Measurement` channels and set the
  channel identity (I2C bus/address, 1-Wire ROM code, ADC input), so
  calibration stays with the sensor.
- The sample interval is a constructor parameter of every driver.

## DHT20 (I2C 0x38)

- Trigger, 80 ms conversion, CRC-checked 20-bit values. The datasheet
  register initialization (0x1B/0x1C/0x1E) runs only if the status byte asks
  for it; the bench module never needed it.
- Agreed with the SHT31 to 0.1 C and 1 %RH. Used as the room reference on LT1.

## SHT31 (I2C 0x44)

- Single shot, high repeatability, no clock stretching, 20 ms wait, CRC per
  word. Soft reset on first contact.
- Conversion uses 64-bit arithmetic: `raw * 175000` overflows 32 bits (the
  first bench reading was -38 C).
- The heater exists although DFRobot does not mention it (Sensirion commands
  0x306D on, 0x3066 off): 60 s heating raised the reading by 3.1 C and lowered
  RH from 51 % to 43 %. Useful to dry an outdoor sensor after fog or dew; not
  used automatically yet (`setHeater()`).

## 1-Wire bus (OneWireBus, can_mcu::OneWirePin)

- Standard speed, open drain with the sensor module's pull-up (the internal
  pull-up is only a backup). Each bit slot runs with interrupts disabled
  (about 70 us); a byte takes about 0.6 ms. Read slots sample 13 us after the
  falling edge (call overhead at -O0 made 15 us too tight).
- One sensor per pin (Read ROM). A different sensor on the same pin is picked
  up with its own identity and settings.
- `OneWireBus::presenceProfile()` diagnoses a silent bus (line driven low?
  presence pulse when?); LT1 exposes it as the `onewire` command.

## DS18B20

- 12 bit, 750 ms conversion, scratchpad CRC plus configuration byte sanity
  check (an all-zero frame passes the CRC), power-on 85 C discarded.
- Bench reading about 0.8 C above the I2C sensors (specified +-0.5 C); use
  `sensor <name> cal`.

## CT1780 thermocouple (SEN0656, family 0x3B)

- Scratchpad layout of the MAX31850 (confirmed: with the thermocouple
  disconnected the fault bit and the open-circuit bit were set):
  thermocouple 14 bit / 0.25 C, cold junction 12 bit / 0.0625 C, DIP switch
  address in the configuration byte.
- The converter reports the end of a conversion through read slots, so the
  driver polls instead of DFRobot's fixed 750 ms: 85..105 ms per conversion,
  about 10 readings per second back to back, 3/s used for the response test.
  At 10/s the bit-banged bus costs about 20 % CPU, at 3/s about 7 %.
- One of the two modules never answered the reset pulse (no presence pulse,
  verified with a scope): defective.
- Not compatible with the SEN0198 PT100 amplifier (a thermocouple is a
  voltage source of a few ohms, the SEN0198 measures resistance). An analog
  thermocouple amplifier (AD8495, 1.25 V + 5 mV/C) would work with
  `AnalogSensor` but ends at about 410 C on 3.3 V; dry exhaust needs the
  CT1780.

## PT100 amplifier (SEN0198, analog)

- DFRobot's "0.2..2.8 V for 30..350 C" is only approximate. DFRobot's library
  uses a bridge formula, now `sensors::RtdBridge{1800, 3976.2, 39.762}`:
  `R = (1800 V + 3976.2) / (39.762 - V)`, then Callendar-Van Dusen. At room
  temperature: 0.240 V = 30.0 C, about 3 C above the reference (within the
  +-2 % FS specification). Calibrate with two points (room, boiling water):
  `sensor <name> cal <ref>`, then `cal2 <ref>`.
- One ADC step is about 0.1 C; readings wander +-0.2 C.
- The ADC is shared through `can_mcu::McuMonitor::addInput()`.
- An unconnected analog input floats (read 1.4 V, i.e. 184 C). The fault
  range only catches the rails; a product needs a pull-down on each analog
  input so a missing sensor becomes a fault.

## VEML7700 ambient light (I2C 0x10)

- ID register 0x07 reads 0xC481. Resolution 0.0036 lx/count at gain 2 and
  800 ms (datasheet revision 2022, as in Adafruit's library), Vishay's
  non-linearity correction at gain 1/4 and 1/8.
- Automatic ranging over nine gain/integration steps (3.6 mlx to 1.84 lx per
  count). Measured about 227 lx indoors, stable to +-0.4 lx. Darkness and
  sunlight ranges not yet checked on the bench.
- No standard NMEA 2000 PGN for illuminance; the channel is not published.

## AS3935 lightning sensor (SEN0290, I2C 0x03, IRQ)

The goal on the boat is a thunderstorm warning ("storm approaching, about N km
away").

What works:
- Configuration per the datasheet (preset, indoor/outdoor gain, noise floor,
  tuning capacitor, RC oscillator calibration with result check).
- Events: lightning (distance 1 = overhead, 5..40 km, 0x3F out of range; raw
  energy without unit), disturber, noise too high. Lightning and noise are
  logged as they come, disturbers as a count every 10 s; all go to the
  driver's listener.
- Adaptive noise floor: "noise too high" raises the threshold (up to 7), ten
  quiet minutes lower it again.
- IRQ (verified on LT1 PA6): the interrupt register is read 2 ms after the
  rising edge (datasheet), with a 1 s safety poll. Without IRQ the driver
  polls (`pollMs`, default 50 ms).
- Automatic antenna tuning with the IRQ (`startAntennaTuning()`, run at every
  start by default): for each of the 16 capacitor steps the resonance / 16 is
  counted on the IRQ pin against the DWT cycle counter, the step closest to
  500 kHz is kept, the oscillators are recalibrated. Takes about 2 s. Bench
  module: 517.7 kHz (0 pF) .. 492.2 kHz (120 pF), about 1.7 kHz per step,
  best 80 pF at 500.3 kHz, repeatable to 0.4 kHz. DFRobot's fixed 96 pF gives
  497.0 kHz (-0.6 %, within the +-3.5 % tolerance). A result outside the
  tolerance is logged as a warning: move the antenna away from metal.

Pitfall found: the calibration shows an oscillator on the IRQ pin for 2 ms.
The datasheet uses the TRCO (32.768 kHz, register 0x08 bit 5). The SRCO
(1.1 MHz, bit 6) would flood a wired IRQ with interrupts and starve the CPU
into a watchdog reset. DFRobot's library sets bit 5 although its comment says
SRCO. The driver ignores IRQ edges during calibration and tuning.

Bench environment (unresolved, expected indoors):
- 5..7 disturbers per second, continuously. Not caused by our I2C polling:
  with polls every 50 ms 23 % of the reads found a disturber, with 500 ms and
  2 s 98..100 %, so an external source emits several pulses per second.
- 50 cm away from LT1 on an extension cable: no change. Wrapped in aluminium
  foil: worse (the foil is a shorted turn around the ferrite antenna and
  detunes it). Suspects: switching supplies, LED drivers, chargers in the
  lab, the CAN controller retransmitting without a peer (error-passive).
- After tuning, "noise too high" pushed the noise floor to 4..5 within a
  second; a correctly tuned antenna is more sensitive.
- No real lightning was observed. A piezo gas lighter near the antenna or a
  function generator driving a small loop with 500 kHz bursts can provoke
  events.

On the boat (open items for the next agent):
1. Wire the IRQ (one EXTI-capable GPIO whose EXTI line is not used by another
   pin; on can-mcu PA6 or PA7 are free, PA4 shares line 4 with button PB4 and
   PA5 line 5 with PB5). Keep automatic tuning at start and check the logged
   frequency table after mounting: the antenna resonance shifts near metal.
2. Mount the antenna away from the CAN bus, chargers, inverters, and engine
   ignition; do not shield it.
3. Start with `reportDisturbers = true` and look at the disturber rate on the
   boat. If it stays high, mask disturbers (`setDisturberReporting(false)`):
   the chip keeps rejecting them, only lightning is reported. Try
   `Location::outdoor` (less gain, fewer false events) if the sensor is
   outside or near a hatch.
4. Not built yet: the storm warning. Plan: a measurement channel "storm
   distance" (km) fed from lightning events, with alarm rules "within 20 km"
   (warning), "within 10 km" (alarm), and a falling-distance rule for an
   approaching storm; it then gets sound, LED, acknowledgement, stored
   thresholds, and logging from the measurement framework. The chip's
   distance is a statistical estimate over the last 17 minutes; the channel
   should go stale (no storm) after about 30 minutes without lightning.
5. Signal K: lightning events and the storm distance have no NMEA 2000 PGN;
   decide how to report them (proprietary PGN or the Signal K plugin).
