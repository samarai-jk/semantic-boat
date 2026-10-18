# Handover: lab-test-01 sensor bring-up → CAN work

TEMPORARY. Written 2026-10-18 when the work moved from the Windows PC to the
Raspberry Pi. Delete this file once its open items are done or moved into
the regular documentation.

Read first: [AGENTS.md](AGENTS.md) (binding rules: code categories, build
profiles, `src/dev/` for dev/test code, verify with the product check).
What is already documented is not repeated here:

- Sensor drivers and bench findings: [src/lib/slstm32/SENSORS.md](src/lib/slstm32/SENSORS.md)
- Measurement channels, alarms, trends, drivers: [src/lib/slstm32/README.md](src/lib/slstm32/README.md)
- Motion sensing (BNO055) data products and ideas: [src/dev/can-mcu/motion-sensing.md](src/dev/can-mcu/motion-sensing.md)
- LT1 device: [src/dev/can-mcu/lab-test-01/README.md](src/dev/can-mcu/lab-test-01/README.md)
- Input module: [src/lib/semantic-modules/12v-ad-input-iso.md](src/lib/semantic-modules/12v-ad-input-iso.md)

## Repository state

- Branch `lab-test-01-bringup`, about 40 commits ahead of `main`, last
  commit `be9c65d`. Not pushed from the PC unless the user did it.
- Uncommitted on the PC, owned by the user, never commit them:
  `.gitignore`, `.vscode/settings.json`, `hardware/can-mcu/can-mcu.eprj2`,
  `hardware/can-mcu/devices/`, `scratch.txt`.
- Builds verified on the PC: `script/build.sh lab-test-01 debug` and the
  product check (`cmake --preset lab-test-01-release -B
  build/lab-test-01/product-check -DSB_DEV_TOOLS=OFF` + build), about 101 KiB
  flash without dev tools.

## Temporary changes that must be reverted

1. **Alarm sounds silenced** (commit `72a219d`, overnight test): in
   `src/dev/can-mcu/lab-test-01/src/app.cpp` restore
   `annunciatorConfig{{&warningSound, &alarmSound, &emergencySound}, ...}`.
2. **rpm frequency test (dev tools)** (commit `acdeb37`): remove
   `src/dev/rpm_test_service.*`, its entries in `dev/dev_tools.cpp` and
   `CMakeLists.txt` (DEV_SOURCES), and the seam hook `devToolsInterrupt()`
   (`dev_tools.hpp`, `dev_tools_disabled.cpp`, the call in
   `HAL_GPIO_EXTI_Callback` in `app.cpp`). Do this **after** item 1 of the
   open work below. The product rpm path (`engineSpeed.onEdge()` in the
   same callback, `board::configureEngineSpeedInput()`) stays.

## Open work, in order

1. **Verify `FrequencyInput` (engine rpm) on hardware.** Bench: TIM2 PWM on
   PA0 (dev command `rpm gen <Hz> [duty %]`) drives an isolated MOSFET
   module that switches 12 V onto input D1 of the 12v-ad-input-iso module
   (1 kΩ load across the MOSFET output); OUT_D_1 arrives on PA5. Expect
   `rpm 3000.00 rpm` in the values log at `rpm gen 750 5` (bench setting 15
   pulses per revolution), proportional at 100/25/1 Hz, 0 after 2 s with
   `rpm off`. The module (I2C 0x49) was unplugged at the end of the
   session.
2. Then remove the rpm test (above) and document the frequency findings in
   `12v-ad-input-iso.md` (not done yet):
   - The digital inputs count at least 750 Hz with 0.33 ms gaps, with the
     standard 100 nF as well as 10 nF (verified on both): an alternator W
     terminal at 3000 rpm (6 pole pairs x 2.5 pulley ratio = 750 Hz) works
     without a hardware change. The real upper limit is unknown (the test
     source was the bottleneck).
   - The 100 nF delays the off edge by only about 0.04 ms, not the 0.7 ms a
     plain R10 x C4 estimate suggests: Q1's base also discharges C4 at
     first.
   - Isolated "PWM MOSFET" modules are useless as a test source: their
     output switches off about 0.9 ms late (optocoupler and gate discharge;
     285 µs output fall even with a 1 kΩ load). A direct NPN or logic-level
     MOSFET switch with a 1 kΩ pull-up would be the right test source.
3. Record the overnight barometer comparison in `SENSORS.md` (not done
   yet): 10.3 h, BMP585 (SEN0664) vs BMP280 (SEN0253): offset 0.05..0.24 hPa
   (typically 0.14), 3 h trends within 0.12 hPa (mean 0.025), no visible
   temperature dependence over a 2.5 °C drop; largest fall -1.24 hPa/3 h,
   no alarm. The curve showed the semidiurnal pressure tide (minimum about
   03:50, rising towards the 10:00 maximum, about ±1 hPa): the 3 hPa/3 h
   warning threshold is well above it.
4. **CAN / NMEA 2000 (the next topic).** Nothing beyond address claim,
   product information, and heartbeat has been verified on a real bus in
   this session; on the PC bench the CAN had no peer (error-passive, TEC
   128, frames not acknowledged). LT1 transmits, all untested on the bus:
   - every 2 s via `n2k::MeasurementPublisher`: 130316 temperatures
     (instances 0 board, 1 mcu, 2 air), 130313 humidity (0), 130314 pressure
     (0 = BMP585), 127488 engine rpm (instance 0);
   - every 1 s via `n2k::AttitudePublisher`: 127250 heading (magnetic, "not
     available" until NDOF and magnetometer calibration >= 2), 127251 rate
     of turn, 127257 attitude (pitch/roll, yaw not available);
   - the transmit PGN list in `app.cpp` (`n2kTransmitPgns`) lists them.
   - Development manufacturer code 2046, preferred address from EEPROM
     (last claimed 36).
   - Conversions were checked by hand (90 deg -> 15707 in 1e-4 rad; 10 deg/s
     -> 5585054 in 3.125e-8 rad/s), not against a decoder yet.
   - The project will define its own proprietary messages for quantities
     without a standard PGN (illuminance, UV, CO2, lightning, motion
     statistics, sensor state), taken into Signal K by configuration or
     plugin. Design them once for all devices.

## Smaller open points (not urgent)

- BNO055: store the 22 calibration bytes (0x55..0x6A) in EEPROM; circular
  handling (359 -> 0) for heading channels (min/max/rate/filter); axis remap
  for the mounting. Details in SENSORS.md / motion-sensing.md.
- SCD41: no forced recalibration command yet (needed if automatic
  self-calibration drifts, or where it must be off: engine room, battery
  box).
- `audio::SoundPlayer` / `can_mcu::DacAudio` compile but never ran on
  hardware in their library form (the bench test used the same algorithms
  as dev code).
- Release builds are effectively -O3: CMake appends its default `-O3
  -DNDEBUG` after the toolchain's `-Os` (`cmake/gcc-arm-none-eabi.cmake` of
  each device). Fixing it should save flash; affects remote-a too.
- The heartbeat's I2C error counter also counts NACKs of absent devices
  (driver retries about once a second each); `rec` (bus recoveries) is the
  number that matters.
- The AGENTS.md product check `strings <elf> | grep -E "DEV BUILD|-dev"`
  matches "host-dev" in LT1's model string; harmless for the lab device,
  but a product must not contain "-dev" in its strings.

## Bench and tooling facts (Windows PC, for reference)

- LT1 at the end: DHT20 (0x38), BMP585 Gravity SEN0664 (0x47, managed
  module), SEN0253 10-DOF (BNO055 0x28, BMP280 0x76), MOSFET module on PA0.
  Not connected: VEML7700, SCD41, AS3935, speaker, 1-Wire sensors, input
  module. PA4 is the 1-Wire bus (input module DI0 not connected on LT1).
- I2C probe = one-byte read, then an empty write if refused (BNO055 and
  Sensirion compatible; see SENSORS.md "Common behaviour").
- On the PC: serial logger `script/serial-log.ps1` (COM4; on the Pi
  `script/serial-log.sh`), commands via `build/logs/serial-command.txt`,
  monitor `script/monitor.ps1`, SWD probe `script/swd-probe.ps1`. The CAN
  bench tool `src/tools/can-bench` ran with a candle (gs_usb) adapter at
  250 kbit/s, sample point 85 % (87.5 % not available on that backend).
- The board drops off the ST-Link when its 12 V is off; a flash then fails
  with "target voltage may be too low".

## Working agreements with the user (not in AGENTS.md)

- Generic, reusable code goes into slstm32 (drivers and services), but
  nothing board-, device-, or module-product-specific; memory is tight (a
  later CAN display board must fit).
- One sensor or feature at a time on the bench; propose a plan and ask
  before larger or hardware-related steps.
- Temporary test aids go into `src/dev/`, are committed as TEMPORARY, and are
  removed when the test is done (AGENTS.md rule 6).
- Commit with the Co-Authored-By trailer; never commit the user's
  uncommitted files listed above; don't push unless asked.
