# lab-test-01 (LT1)

LT1 is the first device built from [can-mcu](../can-mcu.md) +
[host-dev](../host-dev.md). It has three purposes:

1. Bring up and test the can-mcu hardware.
2. Develop the first can-mcu firmware.
3. Evaluate a set of DFRobot sensors (I2C, analog, and one-wire).

LT1 then serves as the template for real can-mcu devices. Wherever possible
its behavior is implemented as generic drivers and services in `slstm32`; only
the board bindings, the composition root, and LT1-specific policy live in this
directory. `slstm32` never receives board- or device-specific code.

## Feature set

| Feature | Hardware | MCU |
| --- | --- | --- |
| 4x I2C sensor connectors | host-dev H1/H2/H3/H6 | I2C1 (PB8/PB9) |
| 4x analog inputs | J2.7, J2.9, J1.10, J1.11 | PA0 to PA3 (ADC1_IN5 to IN8) |
| 4x one-wire inputs (currently the input module's DI0..DI3) | J1.2, J1.4, J1.3, J1.6 | PA4 to PA7 |
| Green status LED (PWM) | host-dev H9.2 -> J2.4 | PA8, TIM1_CH1 |
| Red status LED (PWM) | host-dev H9.1 -> J2.5 | PA9, TIM1_CH2 |
| Passive buzzer (PWM) | host-dev H7.1 -> J2.6 | PA10, TIM1_CH3 |
| 6x buttons BF1 to BF6 | BTN_FUN_0 to BTN_FUN_5 frontends | PC13, PA15, PB2, PB4, PB10, PB11 |
| NMEA 2000 at 250 kbit/s | can-mcu RJ1 | CAN1 (PA11/PA12), PB1 mode, PB0 LED |
| Settings storage | can-mcu AT24C256C (0x52) | I2C1 |
| Enclosure temperature | can-mcu TMP75 (0x4F) | I2C1 |
| Console and logging | ST-Link VCP on CN7 | USART1 (PB6/PB7) |

Not used on LT1: host-load output (HOST_POWER held low), BTN_POWER frontend
and the encoder, the remote header, PB12 to PB14, H6, and the CN6 backup
battery.

## Button mapping

The buttons are wired to the frontends in order, as printed on the silkscreen.

| Button | Frontend | 0 Ω bridge | GPIO | EXTI |
| --- | --- | --- | --- | --- |
| BF1 | BTN_FUN_0 | R47 | PC13 | 13 |
| BF2 | BTN_FUN_1 | R48 | PA15 | 15 |
| BF3 | BTN_FUN_2 | R49 | PB2 | 2 |
| BF4 | BTN_FUN_3 | R50 | PB4 | 4 |
| BF5 | BTN_FUN_4 | R51 | PB10 | 10 |
| BF6 | BTN_FUN_5 | R52 | PB11 | 11 |

All buttons are active low with hardware debounce on the can-mcu.

## Assembly requirements

- can-mcu: bridges R47 to R52 fitted. R53 (BTN_POWER) is not needed.
- host-dev: encoder jumpers on P1 removed, because PA0/PA1 are analog inputs.
- host-dev: remote not connected to H8, because H8.6 is tied to PA2 through
  300 Ω.
- Wiring on host-dev: H9.2 (green) -> J2.4, H9.1 (red) -> J2.5,
  H7.1 (buzzer) -> J2.6.
- 12 V supply on can-mcu CN2 with CN1 bridged or switched. To power the board
  from the backbone, loop CN5 to CN2.

## Hardware notes for firmware

- **Status LEDs are active low** (common anode on +3V3). The PWM outputs are
  inverted: 0 % duty means the output stays high and the LED is off.
- **TIM1 is shared** by both LEDs and the buzzer, so all three channels share
  one prescaler and period. Changing the tone frequency changes the LED PWM
  frequency, and LED compare values must be rescaled whenever the period
  changes. The passive buzzer is loudest near its 2.7 kHz resonance.
- **Analog inputs** have 100 Ω series resistors and 3.3 V ESD diodes. Power
  analog sensors from 3.3 V so their output cannot exceed the ADC range.
- **One-wire inputs** have 33 Ω series resistors. The protocol needs open-drain
  operation with an external pull-up (typically 4.7 kΩ, often on the sensor
  module) and microsecond timing.
- **I2C addresses** 0x52 (EEPROM) and 0x4F (TMP75) are taken on the shared bus.
- **CAN transceiver mode:** PB1 low = high-speed mode while CAN is active.
  The yellow RJ45 LED on PB0 is active low.
- **ADC:** ADC1_IN8 (PA3) shows a CubeMX conflict with PA4. It refers to the
  differential mode of IN8, whose negative input is PA4. LT1 uses IN8
  single-ended, so the warning is harmless.

## Firmware structure

| Path | Content |
| --- | --- |
| `cmx/` | CubeMX project; only `USER CODE` sections are edited by hand |
| `../common/` | can-mcu board support shared by all can-mcu devices |
| `src/app.cpp` | Composition root and the C hooks called from `main.c` |
| `src/board.cpp` | LT1 host bindings: buttons, TIM1 LEDs and buzzer |
| `src/hardware_test_service.cpp` | User-interface bring-up: LEDs, buttons, clicks |
| `src/diagnostics_service.cpp` | Supply dips and the heartbeat |
| `src/can_monitor_service.cpp` | CAN state, activity LED, N2K message log and summary |
| `src/input_module_test_service.cpp` | Bring-up of a 12v-ad-input-iso module |
| `src/temperature_report_service.cpp` | Board and MCU temperature as PGN 130316 |
| `src/i2c_scan_service.cpp` | I2C scan at startup and hot-plug detection |
| `src/device_settings.cpp` | Settings record in EEPROM: boot counter and NMEA 2000 address |

`main.c` calls `lab_test_01_app_init()` after the CubeMX peripheral
initialization and `lab_test_01_app_run()` in the main loop. SysTick calls
`lab_test_01_app_tick()`. `Error_Handler()` and `HardFault_Handler()` call
`can_mcu_fatal_error()`.

## EEPROM layout

The AT24C256 (32 KiB) reserves its last 256 bytes (from 0x7F00) for device
settings. The redundant settings store uses the first 128 bytes of that range;
the rest of the EEPROM is free.

## Build and flash

```bat
script\build.bat lab-test-01 debug
script\build.bat lab-test-01 debug refresh flash
```

`./script/build.sh` takes the same arguments. The VS Code launch
configuration **lab-test-01: Debug (OpenOCD)** builds, flashes, and stops at
`main`.

**First flash of a new MCU:** a blank STM32L4 latches a "flash empty" flag at
power-on and boots the ST bootloader instead of the application. The flag is
only re-evaluated on a power-on reset, so after the very first programming
remove the 12 V supply once. A reset alone is not enough.

## Watching the device

| Tool | Purpose |
| --- | --- |
| `script\serial-log.ps1` (task **Serial log**) | Owns COM4, writes `build/logs/serial.log`, sends console commands |
| `script\monitor.ps1` (task **Monitor**) | Live events in colour (restarts, buttons, supply dips, CAN state, module changes, console), warns when the board goes silent, sends typed commands; `-All` shows every line |
| `script\swd-probe.ps1` (task **SWD probe**, **SWD log (follow)**) | Pin and CAN register probes over SWD into `build/logs/swd.log`; `-Follow` tails it, `-Every <s>` repeats |

Only one serial logger can run at a time. The monitor only reads the logs,
so any number of monitors can run next to it.

Console commands (type into the monitor or the serial logger):

| Command | Purpose |
| --- | --- |
| `help` | List all commands |
| `supply [<mV>]` | Supply voltage now and since the last heartbeat; set the dip threshold (default 3150 mV) |
| `ain` | Terminal and ADC voltages of the input module |
| `health [clear]` | Restarts per cause, supply drops, this and the previous run; `clear` resets the statistics |
| `trap hang\|fault` | Provoke a watchdog restart or a HardFault to verify the traps |
| `cal <addr> [<ch> <volts>\|zero\|reset]` | Show or calibrate an input module channel (see the module notes) |

## Unattended runs

- The independent watchdog (4 s) restarts a hung firmware; the restart is
  logged with cause "independent watchdog" and counted.
- The power monitor counts every VDD drop below 2.9 V by interrupt and logs
  it (`power: VDD fell below 2.9 V`); the ADC-based dip check (`supply`)
  covers slower sags above that.
- A HardFault or `Error_Handler` stores a crash record with the fault status
  registers in the RTC backup registers; after the watchdog restart it is
  logged (`health: previous run ended by ...`) and counted.
- The health record in EEPROM keeps restarts per cause, supply drops, and the
  previous run's duration and lowest supply (saved every minute), so the
  outcome is known even if the PC log was lost.
- The red LED blinks once an issue was seen (latched until the next reset).
- Background load: module values as PGN 127508 and digital inputs as PGN
  127501 every second, temperatures every 2 s, heartbeat every 60 s, I2C
  scans every 5 s, green LED blink every 2 s. The buzzer stays silent.
- On the PC, `can_bench.py --quiet --stats 60` acknowledges the CAN traffic
  and logs statistics and heartbeat gaps.

## Bring-up behavior

Log output: ST-Link VCP, 115200 8N1. Run `script\serial-log.ps1` (or the VS
Code task **Serial log (ST-Link VCP)**) to see it live; the script also
appends it to `build/logs/serial.log`, so the log can be followed in an
editor or by other tools at the same time.

- At startup: firmware banner, reset cause, clock status (HSE/LSE), MCU
  identity; green LED on at 30 %; a short 2.7 kHz beep.
- At startup the EEPROM boot counter is incremented and logged
  (`eeprom: boot #N`), and the I2C bus is scanned. Each device is listed with
  the parts commonly found at its address. The bus is rescanned every 5 s and
  devices that appear or disappear are logged, so sensors can be plugged in
  while running.
- BF1 to BF6: log line, red LED flash, and a quiet click (5 % duty) of 1000,
  1500, 2000, 2700, 3500, and 4500 Hz respectively. Full volume (50 % duty)
  is reserved for alarms. The raw button levels are also polled every 10 ms and logged on change, so a
  wiring fault (no level change) can be told apart from an interrupt fault.
- The supply (VDDA = +3V3) is measured through VREFINT about 23,000 times per
  second. A 100 ms window below 3.15 V is logged as a supply dip.
- NMEA 2000 (see [`src/lib/n2k`](../../../lib/n2k/README.md)): the node claims
  its stored address (default 35) with a NAME whose unique number is derived
  from the MCU unique ID (manufacturer code 2046 for development, device class
  75 "sensor communication interface", function 130 "temperature"). A changed
  address after a conflict is stored in EEPROM. It answers requests for the
  address claim, product information, PGN list, and heartbeat, NACKs other
  requests addressed to it, sends a heartbeat every 60 s, and publishes the
  board temperature (instance 0) and MCU temperature (instance 1) as PGN 130316
  every 2 s.
- CAN monitoring: every controller state change is logged with its error
  counters, the yellow RJ45 LED flashes on traffic, messages passed to the
  application are logged (ten per second at most), and a CAN and N2K summary
  follows every 10 s. Without another node on the bus nothing acknowledges, so
  the controller goes error-passive within milliseconds; that is expected.
  See [`src/tools/can-bench`](../../../tools/can-bench/README.md) for the PC
  side and its test options.
- 12v-ad-input-iso module (ADS1115 at 0x48, DI0..DI3 on PA4..PA7, see
  [`src/lib/semantic-modules`](../../../lib/semantic-modules/12v-ad-input-iso.md)):
  ADC presence changes and every digital input change are logged at once;
  once per second a line with the four terminal voltages and their
  peak-to-peak noise, followed by the digital states and the sample rate.
- A debug heartbeat every 10 s with uptime, supply range, board temperature
  (TMP75), MCU die temperature, I2C bus errors and recoveries, and dropped
  events.
- Reset cause `power-on/brown-out` after an unexpected restart means the
  supply collapsed; `reset pin` means NRST was pulled (button or debug probe).
- Red LED blinking at 4 Hz: a driver or service failed to initialize; the log
  says so.
- Yellow CAN LED blinking at 5 Hz: fatal error (`Error_Handler` or hard
  fault). Before USART1 is initialized, for example when a crystal fails to
  start, there is no log message.
