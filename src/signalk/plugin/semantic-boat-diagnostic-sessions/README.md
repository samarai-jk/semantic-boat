# Diagnostic Sessions

Signal K webapp that shows the diagnostic sessions of Semantic Boat devices
(high-rate recordings such as an engine start;
[DIAGNOSTICS.md](../../../lib/b1s/DIAGNOSTICS.md)). The sessions are fetched
and stored by the Semantic Boat Devices plugin; this app only reads them from
its routes, so it needs a Signal K login with admin rights.

- A list of all sessions (newest first, filter by kind) with their outcome.
- The selected session: key numbers of its kind (engine start: battery at
  rest, preheat time and voltage drop, per crank attempt duration, lowest
  battery and starter voltage, cable loss, compression pulses; time to run,
  charging, idle speed), the values at the start, and the recorded series as
  graphs grouped by unit, with the phases as bands and the switch inputs as
  bars. Point for values, drag to zoom; buttons for the whole session and
  each crank.

Installation (as the other Semantic Boat plugins, a link into Signal K):
`~/.signalk/node_modules/@semantic-boat/diagnostic-sessions` pointing here and
the dependency `"@semantic-boat/diagnostic-sessions": "file:../semantic-boat/src/signalk/plugin/semantic-boat-diagnostic-sessions"`
in `~/.signalk/package.json`; restart Signal K. It appears in the Signal K
menu under Webapps.
