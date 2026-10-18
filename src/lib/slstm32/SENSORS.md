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
| UV index (DFRobot SEN0636) | `DfrobotUvIndexDriver` | I2C 0x23 | Communication verified; UV only seen as 0 indoors; not suited behind ordinary windows |
| BMP585 (DFRobot SEN0664 Gravity) | `Bmp585Driver` | I2C 0x47 (0x46) | Verified, read-only (module ignores configuration) |
| BNO055 + BMP280 (DFRobot SEN0253 10-DOF) | `Bno055Driver`, `Bmp280Driver` | I2C 0x28, 0x76 | Verified (orientation, pressure) |
| SCD41 CO2 (generic module, sold as "SD40/SD41") | `Scd4xDriver` | I2C 0x62 | Verified |
| Engine rpm (12v-ad-input-iso digital input) | `FrequencyInput` | EXTI + cycle counter | Method verified at 750 Hz; driver not yet run on hardware |
| Speaker (DFRobot FIT0449, 8002 amplifier) | `audio::Synthesizer`, `audio::SoundPlayer`, `can_mcu::DacAudio` | DAC PA4/PA5 | Tested as dev code; library form not yet run on hardware |

## Common behaviour

- I2C presence probes (bus scan, `I2cBus::probe()`) first read one byte and
  only if that is refused send an empty write (address + stop,
  `HAL_I2C_IsDeviceReady`). The empty write made the BNO055 misinterpret its
  next register access (fusion stopped with error 9 after every 5 s
  rescan); it answers the read, so it never gets one. Sensirion sensors
  (SCD4x, SHT3x) refuse a read without a preceding command and are found by
  the empty write.

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
  presence pulse when?). During the bench tests LT1 exposed it as the
  service command `onewire` (git history, commit 148915a).

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
- No standard NMEA 2000 PGN for illuminance: it goes into the project's own
  (proprietary) messages, taken into Signal K by configuration or plugin.

## UV index (SEN0636, I2C 0x23)

- A module with its own controller (I2C or UART by a switch on the board),
  not a bare sensor chip. 16-bit registers, little-endian on I2C, sequential
  reads work: 0x00 product ID 0x427C, 0x01 0x3334, 0x02 address 0x23, 0x03
  0x0003, 0x04 0x0001, 0x05 version 0x1000, 0x06 raw UV, 0x07 UV index
  (integer), 0x08 risk level 0..4.
- Address 0x23 collides with the BH1750 light sensor (ADDR low); they cannot
  share a bus.
- Indoors behind glass every value reads 0 (glass blocks UVB and most UVA).
- A bright white LED head torch held close drove the index to 11 (raw 1120;
  index 1 at raw 116, so roughly index = raw / 100, the index register
  lagging the raw value by a reading or two). The module cannot tell the
  spectrum: its photodiode (240..370 nm nominal) still responds to the violet
  and blue tail of a white LED, and the index is the photocurrent scaled for
  sunlight. Artificial light therefore proves only that the chain works; the
  index is meaningful only in sunlight.
- Still to check in sunlight against a reference (weather service UV
  forecast or a calibrated meter): the index scale, and whether raw / 100
  gives a usable index with decimals.
- Quantity `uvIndex` (thousandths of the index); no standard NMEA 2000 PGN,
  so it goes into the project's own messages.

- Behind ordinary acrylic, polycarbonate, or glass the index reads far too
  low (UVB is blocked). A window needs fused quartz (JGS1/JGS2), UV
  transmitting acrylic, or a PTFE diffuser; the sensor also needs a cosine
  diffuser for a low sun. Not used for now; a UV index can instead be
  inferred in Signal K: clear-sky UVI from the sun elevation
  (`12.5 * cos(zenith)^2.42 * (ozone / 300 DU)^-1.23`, Madronich) times a
  cloud factor from a pyranometer (measured / clear-sky irradiance, about
  ^0.6 for UV), or a UV forecast (e.g. Open-Meteo `uv_index`) scaled by that
  cloud factor. Expect +-1..2 UVI.

## BMP585 barometer (SEN0664, I2C 0x47)

- The Gravity module (I2C/UART, DIP switches for mode and address) has a
  controller in front of the BMP585 for its UART/Modbus mode. In I2C mode the
  chip registers read normally (chip ID 0x51, rev 0x32, data at 0x1D..0x22),
  but every write is ignored, including the soft reset: the module keeps the
  chip in normal mode at 1 Hz with oversampling x1 (OSR_CONFIG 0x40,
  ODR_CONFIG 0x71, INT_CONFIG 0x35). DFRobot's library writes the same way
  and never checks whether a setting took effect.
- The status register never shows "NVM ready" (Bosch's API waits for it),
  although the chip measures correctly; the driver only rejects an NVM error.
- `Bmp585Driver` reads its configuration back and switches to read-only
  ("managed") mode: it reads the latest result every period. With a bare
  BMP585/BMP581 (Fermion module or own board) it uses forced mode with
  pressure x16 / temperature x2 and polls the data-ready flag.
- Bench: 1009.84 hPa, 28.2 C die temperature (the die runs about 1 C above
  the room). Published as PGN 130314 (Actual Pressure, atmospheric).
- Pressure tendency (bench configuration, see the git history of
  lab-test-01's `app.cpp`, commit a00900d): a 3-hour channel trend (19
  samples, one per 10 min, `MeasurementConfig::trendWindowMs` and
  `Measurement::setTrendBuffer()`), alarm rules `trendFall` 3 hPa
  (warning) and 6 hPa (alarm), hysteresis 0.5 hPa, tendency labels steady
  below 0.1 hPa, rapid above 3.5 hPa. `sensor <name> trend <min>` shortens
  the window for tests. The trend is lost at a restart (3 h until fully
  valid again); keeping it in the RTC backup registers would be a later
  improvement.

Overnight comparison BMP585 (SEN0664) against BMP280 (SEN0253), 10.3 h on
lab-test-01 (2026-10-17/18), both with the 3-hour trend:

- Offset 0.05..0.24 hPa (typically 0.14), stable; 3 h trends within 0.12
  hPa (mean 0.025). No visible temperature dependence while the room cooled
  by 2.5 C. For weather both are good enough; the BMP585's gel package is
  the better choice for humid, salty air.
- The curve showed the semidiurnal pressure tide (about +-1 hPa, minima
  around 04:00 and 16:00, maxima around 10:00 and 22:00): the largest fall
  was -1.24 hPa in 3 h without any weather. Trend thresholds must stay above
  that; 3 hPa / 6 hPa in 3 h (warning / alarm) did not trigger.

## BNO055 orientation (SEN0253, I2C 0x28)

What the data is for, what goes on the bus (1 Hz summaries), and the
application ideas (marina storm alarm, anchoring, comfort index, waves) are
in [motion-sensing.md](../../dev/can-mcu/motion-sensing.md).

- Chip ID 0xA0, accelerometer 0xFB, magnetometer 0x32, gyroscope 0x0F,
  software 3.17, self test 0xF. The chip answers its chip ID about 550 ms
  after a reset, before it is fully started; the driver waits 700 ms.
- Modes: NDOF (absolute heading, needs a calibrated magnetometer), IMU
  (accelerometer + gyroscope, heading relative to the start, immune to
  magnetic disturbances), AMG (raw). `setMode()`; NDOF by default.
- One 46-byte burst per 100 ms: acceleration, magnetic field, angular rate,
  Euler angles (1/16 degree), quaternion (2^14), linear acceleration,
  gravity, temperature, calibration status. Heading, pitch, roll go into
  angle channels; the fusion status is checked every second and a stopped
  fusion is restarted.
- Bench: pitch -2.00, roll -1.38 deg lying on the desk; with the board moved
  the magnetometer calibration reached 2 and the system 3 within a minute.
- The calibration is lost at every power-up. The 22 calibration offset
  bytes (0x55..0x6A, readable in config mode) should be stored and written
  back at startup, so the heading is valid immediately (open).
- Heading is circular: the channel's minimum, maximum, rate, and smoothing
  do not handle the 359 -> 0 wrap yet (open: a circular flag on the
  channel).
- Mounting: the Euler angles assume the chip's default axes; a boat
  installation needs the axis remap (AXIS_MAP_CONFIG/SIGN) for its mounting
  position, and a magnetic deviation table for an absolute heading.

## BMP280 (SEN0253, I2C 0x76)

- Chip ID 0x58 (0x60 would be a BME280; its humidity is not read). Forced
  mode, pressure x16, temperature x2, Bosch's integer compensation.
- Bench: 1010.07..1010.13 hPa, within 0.25 hPa of the BMP585 measured
  earlier the same day.

## SCD41 CO2 (I2C 0x62)

- The module is labelled "SD40/SD41"; the sensor identifies itself as an
  SCD41 (get_sensor_variant 0x202F), serial 57D003073B89, i.e. a genuine
  Sensirion part or a faithful copy.
- Periodic measurement (one result every 5 s, the driver polls "data ready"
  every second), CRC per word. A measurement left running by a previous
  MCU run is stopped first (500 ms).
- Pressure compensation: LT1 passes the barometer value (BMP585, else
  BMP280) every minute (set_ambient_pressure, accepted during measurement).
- Bench: 577..599 ppm indoors (room air with people, plausible), sensor
  temperature 27.1..27.5 C (Sensirion's default 4 C self-heating offset
  applied), humidity 49..51 %, close to the DHT20.
- Automatic self-calibration is left on (sensor default): it needs fresh air
  (about 400 ppm) at least once a week, which a boat cabin usually gets;
  in a permanently closed boat it drifts. The SCD41 also has a single-shot
  mode for low power (not used).
- No standard NMEA 2000 PGN for CO2: the project's own messages.

## Speaker (FIT0449 on the DAC)

- An 8002 class-AB amplifier with a 0.5 W / 8 ohm speaker and a volume
  potentiometer; despite "digital" in the name the input is analog. Driven
  from the STM32L431's 12-bit DAC (PA4 = channel 1, PA5 = channel 2) at a
  16 kHz sample rate from a TIM6 interrupt (about 3 % CPU with three
  oscillators).
- Bench at 3.3 V supply (no 5 V on host-dev; 5 V would give about 2.5 times
  the power): at 2.7 kHz about as loud as the passive buzzer at full power.
  Both are loud enough to wake a sleeper next to them; whether either is
  heard over a 50 hp diesel at 2200 rpm 2 m away (easily 90..100 dB) is to
  be tested on board.
- Sound repertoire tried: sine/square/triangle tones, frequency sweep,
  wail and yelp sirens (600..1300 Hz), two-tone hi-lo 960/770 Hz, beep
  groups, general alarm (7 short + 1 long), "have a look" chirps, bell
  chimes (fundamental plus two decaying inharmonic partials). Now in
  `audio::SoundPlayer`; the library form compiles but has not run on the
  hardware yet (the bench test used the same algorithms as dev code).
- Not tried yet: recorded sounds or speech (8 kHz PCM / 4-bit ADPCM from
  flash, about 4..8 KiB per second; Talkie/LPC or SAM for synthetic
  speech).
- Usage: `audio::Synthesizer synth{16000}; audio::SoundPlayer player{runtime,
  synth}; can_mcu::DacAudio out{synth, can_mcu::DacAudio::Pin::pa4};`, then
  `out.start()` after init and e.g. `player.wail(10000)`.

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
5. Lightning events and the storm distance have no standard NMEA 2000 PGN;
   they go into the project's own (proprietary) messages, taken into Signal K
   by configuration or plugin.
