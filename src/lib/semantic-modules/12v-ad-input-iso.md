# 12v-ad-input-iso

Isolated input module for 12 V marine signals: four analog inputs and four
digital inputs, galvanically isolated from the host, with surge protection on
every input. Driver: `semantic_modules::AdInputIso12v`
([ad_input_iso_12v.hpp](include/semantic_modules/ad_input_iso_12v.hpp)).

Hardware source: `hardware/can-mcu/can-mcu.eprj2`, schematic and PCB
**mod-input-isolated**. Details below come from the document snapshot stored
in that file (2026-09-06); check them against the current design when in doubt.

## Connectors

| Ref | Type | Pins |
| --- | --- | --- |
| CN1 | JST PH 4-pin, host side | 1 +3V3, 2 GND, 3 SCL, 4 SDA |
| H1 | 1x4 2.54 mm, host side | OUT_D_0 .. OUT_D_3 (digital inputs to the MCU) |
| P1 | 3.81 mm 5-pin terminal, isolated | 1..4 analog inputs A0..A3, 5 GND_I |
| P2 | 3.81 mm 5-pin terminal, isolated | 1..4 digital inputs D0..D3, 5 GND_I |

CN1 matches the host-dev I2C connectors (3V3, GND, SCL, SDA). The host
provides the I2C pull-ups on its side.

## Isolation

- B0303S-1W isolated DC/DC converter from the host 3.3 V to 3V3_I, with a
  ferrite and a 3.6 V Zener clamp (the converter is unregulated).
- ISO1541 bidirectional I2C isolator, 4.7 kΩ pull-ups on the isolated side.
- ISO7740 four-channel digital isolator towards the host (outputs on H1,
  push-pull from the host 3.3 V, default high when the isolated side is
  unpowered).
- Everything on P1/P2 is referenced to GND_I, which is not connected to the
  host GND.

## Analog inputs (A0..A3)

Per channel: PTC fuse (nSMD005, 50 mA) → SMBJ24CA TVS → 82 kΩ / 10 kΩ 0.1 %
divider (ratio 9.2) with 100 nF → BAV199 clamp to 3V3_I/GND_I → 1 kΩ → ADS1115
AINx (single-ended against GND_I).

- ADS1115 address: 0x48 with R33 (ADDR to GND), 0x49 with R34 (ADDR to VDD).
  ALERT/RDY is not connected.
- Range at the terminal: ±2.048 V full scale gives 0..18.8 V (0.58 mV per
  count), suitable for 12 V systems; ±4.096 V gives up to 37.7 V, but the
  clamp (about 30 V) and the TVS (24 V working voltage) limit the usable range
  to about 24 V.
- Input impedance about 92 kΩ. The divider source impedance (about 10 kΩ)
  against the ADS1115 input impedance causes a gain error of a few tenths of a
  percent; calibrate each channel for accuracy (see Calibration below).

## Digital inputs (D0..D3)

Per channel: 10 kΩ → SMBJ24CA TVS → 1N4148 → 100 kΩ / 22 kΩ divider with
100 nF → NPN → ISO7740 → H1 OUT_D_x.

- Switching threshold about 4 V at the terminal (diode drop plus 0.6 V at the
  transistor base through the divider): 12 V signals switch cleanly, below
  about 3 V reads off.
- RC filter time constant about 2 ms.
- **Inverted**: OUT_D_x is low while 12 V is present. The driver accounts
  for this; the board binding reports the physical pin level.
- An open input reads off. A host pin with a pull-up also reads off while the
  module is unplugged.

## Driver configuration

```cpp
semantic_modules::AdInputIso12vConfig config{
    0x48u,                                            // ADS1115 address
    slstm32::drivers::Ads1115Driver::Gain::fsr2048,   // 0..18.8 V
    slstm32::drivers::Ads1115Driver::DataRate::sps128,
    board::isolatedModuleInputs(),                    // MCU pins for OUT_D_0..3
    20u,                                              // debounce, ms
};
semantic_modules::AdInputIso12v module{runtime, i2cBus, config};
```

Unconnected digital inputs use a binding without a read callback. With
128 samples/s each analog channel refreshes about 27 times per second.

## Calibration

`AdInputIso12vCalibrator` keeps one calibration per module, keyed by the
ADS1115 address, in the host EEPROM (`AdInputIso12vCalibrationStore`, one
128-byte power-loss-tolerant slot per address 0x48..0x4B). Per channel it
stores a scale factor and a zero offset: terminal mV = ADC uV x scale / 1000
- offset. Stored calibrations are applied at startup.

With the console commands registered:

1. Optional: short the input to GND_I (P1.5) and run `cal <addr> <ch> zero`.
2. Connect a steady reference to the input, measure it at the terminal, and
   run `cal <addr> <ch> <volts>`, for example `cal 0x49 1 11.972`.
3. Check with `ain`.

Automatic mode for bench work: `cal <addr> auto <volts>` waits up to ten
minutes and calibrates every channel the reference touches, one after
another (0.3 s to settle, 24 averaged samples). The calibrator reports its
progress through `onAutoEvent()`; lab-test-01 turns that into feedback on the
board: red LED while measuring, green flash and a quiet click when a channel
is done, fast red blinking when the contact was lost.

First calibration (module 0x49, 12.780 V reference): scales 9.2249, 9.2209,
9.2228, 9.2200, all 0.22..0.27 % above the design value 9.2, as expected from
the ADC input loading the divider. Repeating A0 gave 9.2250 vs. 9.2249.

A single-channel measurement averages 32 samples (about 1.2 s) and is rejected if the scale
deviates more than 5 % from the design value 9.2 or the offset exceeds
100 mV. Several channels can share one reference by bridging their terminals.
