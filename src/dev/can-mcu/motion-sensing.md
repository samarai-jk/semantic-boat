# Motion sensing: data products and application ideas

Design note for a can-mcu device with an orientation sensor (bench-tested:
BNO055 on the DFRobot SEN0253, driver `slstm32::drivers::Bno055Driver`; bench
findings in [SENSORS.md](../../lib/slstm32/SENSORS.md#bno055-orientation-sen0253-i2c-0x28)).
It records what the data is for, what goes on the bus, and the application
ideas for later. Status: standard 1 Hz output implemented on lab-test-01;
everything under "Applications" is an idea, not code.

## Purpose

The sensor is not for navigation: the autopilot has its own, better heading
sensor. It is for

- an always-on heading (also at anchor and in the marina, when the autopilot
  is off),
- recording pitch, roll, and yaw rate for later sail and passage analysis in
  Grafana,
- experiments with motion-derived information (see Applications).

Nothing needs a fast stream on NMEA 2000 or in Signal K. The device reads the
sensor at 10..20 Hz internally and sends summaries.

## Data products and rates

Implemented (lab-test-01, `sensors::AttitudeSummarizer` +
`n2k::AttitudePublisher`): one summary per second from the 10 Hz samples.

| Data | Message | Rate | Notes |
| --- | --- | --- | --- |
| Heading, circular mean of the second | PGN 127250 Vessel Heading, magnetic | 1 Hz | "Not available" unless NDOF mode and magnetometer calibration >= 2 |
| Yaw rate, mean | PGN 127251 Rate of Turn | 1 Hz | Turning to starboard positive |
| Pitch and roll, mean | PGN 127257 Attitude (yaw not available) | 1 Hz | Pitch bow up, roll starboard down positive (signs to be fixed with the mounting) |

Proposed, own (proprietary) messages, taken into Signal K by configuration
or plugin:

| Data | Rate | Why |
| --- | --- | --- |
| Pitch and roll minimum, maximum, standard deviation of the second | 1 Hz | A mean hides peaks; heel extremes and motion spread for sail analysis |
| Yaw rate maximum of the second | 1 Hz | Tacks, gybes, broaching |
| Sensor state: mode, calibration (system/gyro/accel/mag) | on change, else every 60 s | Whether the heading can be trusted |
| Application results (below) | 1/min .. 1/20 min | Small, slow values |

Bus load: three single-frame PGNs per second plus one or two own frames is
well below 1 % of a 250 kbit/s bus. The summary is computed in
`AttitudeSummarizer` (int64 sums, circular heading mean); the log shows it
every 10 s (`attitude: ...`).

Not sent: raw acceleration, magnetic field, quaternion, 10 Hz angles.

## Open points for a real device

- Store the BNO055 calibration offsets (22 bytes, registers 0x55..0x6A,
  config mode) in EEPROM and write them back at startup, so the heading is
  valid immediately instead of after a calibration dance.
- Mounting: axis remap (AXIS_MAP_CONFIG/SIGN) for the installed orientation
  and the sign conventions above; a magnetic deviation table if the heading
  is to be absolute.
- Heading channels are circular: minimum, maximum, rate, and smoothing of a
  measurement channel do not handle the 359 -> 0 wrap yet.
- Define the proprietary message set (one design for all Semantic Boat
  devices, not per sensor).

## Applications (ideas)

Rated by the user on 2026-10-17. Computation estimates for an STM32L431
(80 MHz Cortex-M4F with FPU, 64 KiB RAM); lab-test-01 uses about 12 KiB.

### Marina movement / "storm in the marina" alarm (high interest)

While moored, the boat should hardly move. Wind and swell show as roll,
pitch, yaw, and surge accelerations working against the lines.

- Measure: a motion intensity per minute, e.g. RMS of roll and pitch rate
  plus RMS of horizontal acceleration, and the number and size of jerks
  (lines snatching, fenders hitting).
- Alarm: intensity above a threshold for some minutes: "storm in the marina",
  with intensity and duration; ends with an all-clear. Particularly useful
  when nobody is on board (Signal K notification to the phone).
- Also logs wash from passing boats as short events.
- Cost: a few filters at 10..20 Hz, negligible.

### Anchoring: swing and dragging indicators (high interest)

- Swing: heading range and period over 10..30 min. "Sailing at anchor" shows
  as a regular yaw oscillation of +-30..60 deg; a 180 deg turn at the turn of
  the tide is normal and can be recognized as a single slow turn.
- Snatching: surge-acceleration jolts when the rode comes tight (suggests a
  snubber or more scope; an early sign of dragging).
- Dragging: the swing pattern stops and the boat keeps one heading while
  drifting downwind. The IMU sees the pattern change, GPS sees the movement;
  together more reliable than either. The combination belongs in the
  Signal K plugin (GPS, anchor position, notifications); the device sends
  swing range/period and snatch counts per minute.

### Anchoring comfort index (high interest)

How much and how the boat moves at anchor, comparable between anchorages.

- ISO 2631-1: frequency-weighted vertical acceleration (Wf, the motion
  sickness band 0.1..0.5 Hz) integrated to a Motion Sickness Dose Value
  (MSDV); plus roll amplitude and roll period.
- Output: a comfort index (e.g. 0..10) and its components per minute, into
  Grafana; "anchorage A was twice as rolly as B".
- Cost: IIR filters at 10..20 Hz, negligible.

### Wave height and period (interesting, demanding)

- Vertical acceleration in the earth frame (component along the gravity
  vector), downsampled to about 4 Hz.
- Spectral method as on wave buoys: every 20 min a Welch-averaged FFT
  (e.g. 1024 points); heave spectrum = acceleration spectrum / (2 pi f)^4;
  significant wave height Hs = 4 sqrt(m0), peak period, mean period.
  Double integration in the time domain does not work (drift).
- Limits: swell longer than about 20 s (below 0.05 Hz) disappears in the
  sensor noise; the hull filters short waves; under way the encounter
  frequency shifts with speed and course. Best at anchor or drifting;
  expect +-20..30 %.
- Cost: a 1024-point float FFT takes about 1 ms (CMSIS-DSP) every 20 min,
  8..16 KiB RAM for buffers.

### Further ideas (liked)

- Slamming: vertical acceleration peaks when beating into waves (sail and
  course analysis, "you are stressing the boat").
- Automatic tack and gybe detection: heel sign change with a yaw rate spike;
  logged with time and duration for Grafana.
- Heel alarm: heel above a threshold for some seconds (fits the measurement
  alarm framework).
- Compass cross-check against the autopilot heading (drifting compass, new
  magnetic disturbance).

### Not pursued

- Water ingress from a slowly growing list: bilge alarms are simpler, more
  reliable, and trigger much earlier.
- Impact detection with automatic SAR alarm (e.g. hit by a motor boat): a
  false SAR alarm from a home-made device is too costly; not now. A purely
  local event log of shocks remains possible.

## Where the computation runs

- Device: everything that needs the 10..20 Hz samples (summaries, comfort
  index, marina motion, wave spectrum, slamming, tack detection, heel
  alarm). It works even when the Raspberry Pi is down.
- Signal K plugin: everything that needs GPS, history, or other boats' data
  (anchor dragging, anchorage comparisons, notifications to the phone).
