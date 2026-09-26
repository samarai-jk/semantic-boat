# disp-sim

`disp-sim` is a development gateway for Semantic Boat displays. It connects to
the ST-Link virtual COM port, reads the display's requested subscriptions, and
generates plausible values for those Signal K-style paths.

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

The display configuration controls which values are sent. Changing section on
the device produces a new subscription snapshot and the simulator immediately
switches its generated source set. Permanent history sources remain active.

The development firmware runs the embedded test configuration from RAM when
configuration storage is unavailable and suppresses the corresponding EEPROM
warning modal. The simulator retries its handshake and reports whether it is
receiving no bytes, malformed frames, or a display without a subscription
snapshot.

The wire format is documented in
[../../lib/semantic-link/PROTOCOL.md](../../lib/semantic-link/PROTOCOL.md).
The synthetic generator is intentionally isolated in `src/index.js` so a future
Signal K WebSocket/plugin adapter can replace it without changing framing or
firmware code.

Remote-a defaults to this ST-Link/USART1 development transport. For a production
RS485 build, configure CMake with `-DREMOTE_A_USE_STLINK_DATA_LINK=OFF`; the same
protocol service is then bound to USART2 and its hardware-managed driver-enable
pin.
