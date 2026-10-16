# Simulator roadmap

## Goals

Produce repeatable sailing-vessel instrument data while off the boat, primarily
for Semantic Boat hardware, alarms, and processing. Fidelity should improve
test coverage, not hide faults behind a complicated model. Keep all physical
bus output opt-in and use a separate development Signal K server.

## Architecture

```text
Live controls / saved scenario
            |
            v
Deterministic boat state (SI units, simulation clock)
            |
            v
Instrument schedules -> sensor overrides / fault injection
            |
            +--> canboat encoder --> local N2K TCP --> Signal K decoder
            |
            +--> direct Signal K deltas (alternative consumer-testing path)
            |
            +--> future opt-in virtual/physical CAN adapter

State + output diagnostics --> browser helm UI
```

Separate simulation from instrument scheduling and transport. Reuse
GeographicLib for geodesics and canboat for PGN encoding. Keep simulation time
independent from wall-clock timestamps and make scenarios reproducible.

## Milestones

1. **Foundation (implemented):** validated controls; marina, anchor, underway;
   trip target; current/wind; engine controls; waves/temperature/depth;
   representative PGNs; local TCP; plugin lifecycle; API; automated tests.
   Native decoding, source attribution, real plugin discovery and mounting verified
   against an isolated Signal K 2.33.0 instance; live hardware remains untested.
2. **Live helm UI (implemented):** plugin-served browser app, map with departure/destination
   editing, mode selector, trip progress, engine switch/RPM, wind/current,
   waves, temperature/depth controls, and live output/source diagnostics.
   Standalone preview uses the same control API. Desktop/mobile screenshots,
   route/depth edits, engine controls, pause/reset, map tiles and resize verified.
   UI controls display nautical units; the engine remains SI.
3. **Test scenarios:** saved named presets, reset/replay, seeded gusts, sensor
   dropout/stale/frozen values, noise/spikes, low oil pressure, overheating,
   shallow water, and timed fault sequences. Show active faults explicitly.
   Preserve normal instrument rate independently of fault effects.
4. **Trip fidelity:** multi-leg routes, sail/motor/idle transitions, warm-up and
   fuel use, anchored velocity consistency, improved swell/chop/current,
   arrival behavior, configurable vessel parameters. No polar optimization
   required; optional simple sail response must remain explainable.
5. **Hardware transport:** GNSS/time PGNs, device inventory and address claims,
   configurable device source addresses, virtual CAN first, then opt-in physical
   CAN with rate limits and explicit live-network safeguards. Capture/replay
   logs and compare real versus synthetic instrument traffic.

## Verification

- Deterministic scenario tests, unit conversions, atomic control validation.
- PGN encode/decode round trips and exact rate tests with an injected clock.
- Lifecycle: disable/re-enable, disconnect/reconnect, port conflicts, shutdown.
- Actual Signal K integration: separate N2K and direct-delta modes, correct
  sources and units, actual server security/router behavior, hardware consumers.
- UI: live edits, pause/reset, reconnect/error states, desktop/mobile screenshots.
- Fault sequences: verify alarms fire, clear, become stale, and recover as expected.

## Open decisions

- Which development Signal K server/version and connection provider to use?
- Preferred sailing area, home marina, vessel dimensions, engine, and sensors?
- Should multiple simulated devices have distinct N2K source addresses?
- Which faults and alarm thresholds matter most for the first hardware tests?
- Is initial CAN testing virtual-only, or is there a dedicated isolated test bus?