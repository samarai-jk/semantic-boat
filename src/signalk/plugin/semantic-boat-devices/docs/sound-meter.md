# RS485 Sound Meter Commissioning Notes

Recorded 2026-10-12. Evidence: live read-only probing of the isolated meter and
Chinese product/protocol screenshots supplied by the owner. The exact model and
firmware revision have not been independently established. Screenshots describe
multiple supply-voltage variants and disagree about the default baud rate.

## Verified On This Device

| Setting | Result |
| --- | --- |
| Protocol | Modbus RTU; CRC-valid responses |
| Serial format | 9600 baud, 8 data bits, no parity, 1 stop bit |
| Slave address | 1 |
| Measurement read | Function 03, holding register 0x0000, quantity 1 |
| Documented interpretation | Unsigned register value / 10, in dB(A) |

Read request, including Modbus CRC low byte first:

```text
01 03 00 00 00 01 84 0A
```

Observed response:

```text
01 03 02 01 BF F8 64
```

`0x01BF` = 447, so this reading is **44.7 dB(A)**. A later block read returned
406 at register 0, corresponding to **40.6 dB(A)**. The conversion and A weighting
come from the supplied documentation, not from an independent acoustic calibration.

Function 04 also returned valid data, but is not specified by the supplied
protocol sheet. Use the documented function 03 for integration.

Additional exploratory reads:

| Start register | Quantity | Raw values |
| --- | --- | --- |
| 0x0000 | 8 | 406, 422, 423, 0, 0, 0, 0, 0 |
| 0x0100 | 4 | 0, 0, 0, 0 |

Registers 1 and 2 are **undocumented**. Do not label them as minimum, maximum,
average, or another measurement. Zero responses elsewhere do not establish a
supported register map.

No address, baud, calibration, or other configuration writes were sent.

## Supplied Specifications

- Measurement range: 30-130 dB, A-frequency weighting.
- Frequency response: 20 Hz-12.5 kHz.
- Mode jumper shorted: fast mode, 500 ms update/response interval.
- Mode jumper open: slow mode, 1.5 s update/response interval.
- Reference calibration: 94 dB at 1 kHz, reference pressure 20 micropascals.
- Claimed accuracy: +/-0.5 dB at the reference sound level and frequency;
  this is not a verified accuracy claim for all operating conditions.
- One specification gives 9600 N 8 1; a separate seller notice says default
  address 1 and 4800 baud. The tested unit responds at 9600; do not change it
  merely to match the seller notice.

## Documented Configuration Registers

These settings are documented but were **not read or changed** during the probe.
Addresses below are zero-based Modbus wire addresses, not PLC register numbers.

| Wire address | PLC notation | Meaning |
| --- | --- | --- |
| 0x0000 | 40001 | Instantaneous sound level, raw value is ten times dB(A), read-only |
| 0x07D0 | 42001 | Slave address, documented range 1-255, default 1, read/write |
| 0x07D1 | 42002 | Baud selection: 0 = 2400, 1 = 4800, 2 = 9600, read/write |

Standard Modbus individual slave addresses are 1-247; address 0 is broadcast.
The vendor's wider range does not make 248-255 portable Modbus addresses.
The sheet does not establish a supported write function or any higher baud code.
Do not guess a code for 115200 or issue undocumented writes.

## Wiring And Power

| Pin | Label | Documented use |
| --- | --- | --- |
| 1 | 10-28V | Supply input for the optional 10-28 V variant only |
| 2 | +5V | Supply input for the 4.5-5.5 V variant only |
| 3 | TXD/A | RS485 A on this variant; TTL TX on a different interface variant |
| 4 | RXD/B | RS485 B on this variant; TTL RX on a different interface variant |
| 5 | AO | Reserved |
| 6 | GND | Supply negative |

Confirm the actual board label and connector orientation before wiring. Do not
apply 12/24 V to a 5 V variant or energize both supply inputs based on this table.
The TTL variant uses 0-3.3 V signals and is not an RS485 interface.

## Integration Guidance

- Poll address 1 with function 03, register 0, quantity 1 at 9600 8N1.
- A 1.5-second polling interval is a conservative starting point with unknown
  jumper state. Faster polling does not necessarily yield a new measurement.
- Preserve dB(A) semantics: this is an A-weighted logarithmic sound-pressure
  level, not pressure in pascals or a linear quantity.
- Select an appropriate Signal K path and unit metadata before publishing.
  No Signal K sensor integration has been implemented yet.
- On timeout, CRC failure, or exception, report unavailable/stale data rather
  than a fabricated zero sound level.
- The display bus currently requires 115200 baud. This meter has no documented
  115200 setting. Use a separate serial channel for the meter/sensor network;
  a two-channel adapter has been ordered, but not commissioned.
- Each physical channel needs a single coordinated master. Do not let a sensor
  probe and the gateway open the same channel concurrently.

## Probe Environment

The test used the FTDI port
`/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_BG03SD1E-if00-port0`, resolving to
`/dev/ttyUSB0`, with both displays disconnected.

The probe used the `modbus-serial` library installed temporarily under
`/tmp/semantic-boat-rs485-probe`. That installation is not a project dependency
and may disappear after reboot or temporary-file cleanup.

At the end of commissioning, the port was closed and the Semantic Boat Devices
plugin was left disabled at the owner's request, pending an isolated weather
station test. Signal K itself was left running. This is a dated test state,
not a permanent deployment configuration.