# disp-sim

`disp-sim` is a development gateway for Semantic Boat displays. It connects to
the ST-Link virtual COM port, reads the display's requested subscriptions, and
generates plausible values for those Signal K-style paths.

While connected, the simulator accepts single-key alert commands:

- `i`: Info
- `w`: Warning
- `a`: Alarm
- `e`: Emergency
- `x`: clear all simulated alert conditions

Every level-key press creates a separate alarm with a unique ID and a numbered
title, including repeated presses of the same key. This makes it possible to
test multiple queued alarms of the same severity. Device **ACK** removes its
presentation everywhere while the
simulator retains the acknowledged source condition, **SNOOZE** removes it for
10 seconds and then repeats it, and **SILENT** broadcasts silence while leaving
the visual alert active. **HIDE** is intentionally device-local and produces no
protocol message. Press `x` to rectify and delete the simulated conditions.

## Setup

From this directory:

```powershell
npm install
npm test
npm run ports
```

Flash remote-a, then run:

```powershell
npm start
```

The simulator automatically selects a single ST-Link virtual COM port. If there
are several ST-Link devices, bypass npm argument forwarding and select one
explicitly with `node src/index.js --port COM7`.

The defaults are 115200 baud and a 500 ms update interval for sources whose
configuration does not request a period. Override them with `--baud` and
`--interval`; add `--verbose` to log every protocol frame.

## Display configurations

Put JSON display configurations in [`configs`](configs). The filename is the
configuration name used by the protocol; the device omits the final extension,
so `navigation.json` appears as `navigation`. Only regular `.json` files with filenames of at most 47
UTF-8 bytes are advertised. A different directory can be selected with:

```powershell
node src/index.js --port COM7 --config-dir D:\display-configs
```

The bundled examples are `navigation.json`, `environment.json`, `engine.json`,
and `system.json`. They exercise coherent, slowly changing marine data. The
System configuration deliberately includes both simulated Signal K server
health and remote-client link/software pages, making it suitable as a future
real diagnostics display as well as a protocol test.

Remote-A accepts configuration files up to 8 KiB over this live link. Its SWD
staging path remains separate and retains the larger 16,368-byte limit.

On Remote-A, hold the Down button (`BTN1`) for one second to open **Setup**, then
select **Apps**. The device sends a list request and immediately continues
running normally. A short Down press retains its normal section-navigation
behavior. When the list arrives, an Apps selection modal opens. Up and Down
move the cursor immediately. The unlabelled Previous and Next buttons are
alternative Cancel and Select controls; the two action buttons show Cancel and
Select. `DEFAULT (LOCAL)` is always the first entry and activates the device's
built-in local clock/status configuration without contacting the server.
Selecting a server file only sends a request and returns to the normal display.
If and when the server independently sends the JSON, the device receives it in
the background, compiles and activates it, sends a fresh subscription snapshot,
and attempts to persist the compiled package in EEPROM.

The protocol does not require a request. Run with `--push-list` to send the
directory listing when the display announces itself, demonstrating an
unsolicited server-to-device list. The device also accepts an unsolicited
configuration transfer. List and file transfers are independent and do not
block value updates.

The display configuration controls which values are sent. Changing section on
the device produces a new subscription snapshot and the simulator immediately
switches its generated source set. Permanent history sources remain active.

With no stored or SWD-staged package, Remote-A runs an unmistakable built-in
`LOCAL DEFAULT` page containing only local status and clock widgets. At boot it
probes the board's AT24C256C at the schematic-strapped 7-bit address `0x52` and
shows an error if the device does not acknowledge. A successful configuration
commit followed by a reset verifies the complete EEPROM write/read path. The
simulator retries its handshake and reports whether it is receiving no bytes,
malformed frames, or a display without a subscription snapshot.

The wire format is documented in
[../../lib/semantic-link/PROTOCOL.md](../../lib/semantic-link/PROTOCOL.md).
The synthetic generator is intentionally isolated in `src/index.js` so a future
Signal K WebSocket/plugin adapter can replace it without changing framing or
firmware code.

Remote-a defaults to this ST-Link/USART1 development transport. For a production
RS485 build, configure CMake with `-DREMOTE_A_USE_STLINK_DATA_LINK=OFF`; the same
protocol service is then bound to USART2 and its hardware-managed driver-enable
pin.
