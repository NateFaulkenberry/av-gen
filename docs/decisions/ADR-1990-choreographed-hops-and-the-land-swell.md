# ADR-1990: Choreographed hops, and the swell's reach onto the land as a parameter

- Status: Accepted (Musical Geography CP-D r2, 2026-11-10), scene/musical-geography. Extends ADR-1964 and ADR-1953.
- Code:
  - the hop's `cueOnly`, `maxWait`, `cueEvery`, `cueGap` and `airBeats` (`src/entity/behaviors.cpp`);
  - `env/tiles/waveN/onLand` (`Composition::TileDynamicsParams::waveOnLand`).
- ADR numbers: MG engineering's 1950–1969 ran out with CPD-R1. 1990–1999 were unused (the app-stalls range stopped at
  1981, and 1982–1989 stay reserved for it), so MG continues there.

## Context

The owner chose two items from the reactivity menu: B, the swell reaches the land; and E, choreographed hops. Both
must be smooth: "that's looking quite twitchy now". Art's spec:
- hops at most about one every couple of seconds per alien;
- every audio-driven value moves less than about 5% of its range per frame at 30 fps.

In draft3 the beat cue fired every resting alien on every beat.

## Decision

**E: the hop's cue becomes choreography.**

| Setting | Live parameter | What it does |
|---|---|---|
| `cueOnly` (0/1) | no | A resting body waits for a cue instead of its random pause. `maxWait` (s, default 12) still lets it move in silence. Each answered cue moves it exactly one hop: the first step of its plan, hopped. Its motion is the cues' rhythm and nothing else. |
| `cueEvery` N | no | It answers every Nth cue, at its own place in the cycle (from its seeded stream), so a cast answers in turns rather than all at once. |
| `cueGap` (s) | yes | The least time between two cues it answers. |
| `airBeats` | yes | A hop lasts this many beats at the beat clock's tempo (`beat.bpm`, 30–300), so a cue on a beat lands on a beat. 0 uses `airTime`. |

All of it is deterministic: the cue arrives through an entity reaction (which a seek replays), and the phase is drawn
from the body's own stream at reset.

**B: the swell's reach onto the land is a parameter.** Each wave's `onLand` weight is now `env/tiles/waveN/onLand`, so
art can key it per arc state or route it. Smoothness is the route chain's job: `attackMs` and `decayMs`, which now
actually run, since flat chain keys are refused. Feet ride whatever swell is drawn: the bodies stand on the drawn top,
and the GPU readback test holds that to 1 mm.

The flight's clearance reads the authored waves. So author `onLand` in the scene, and use the parameter to modulate
within the flight's `amplitudeHeadroom`.

## Consequences

- Test: at 120 BPM, a cue on every beat for 60 s, with cueOnly, cueEvery 4, cueGap 2 s and airBeats 2. Result: 8–31
  launches, never closer than 2 s, and every hop exactly two beats (1 s). A cue-only body with no cue rests until
  `maxWait`.
- C (beat pulses on the edge glow) was cancelled by the owner. No engine part was built.
- Day edges are art data: the day state's edge gain, or a key on `env/tiles/glow/contour`. No engine change.
