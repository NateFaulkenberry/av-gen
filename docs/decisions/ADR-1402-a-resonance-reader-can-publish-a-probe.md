# ADR-1402: A resonance reader can publish a probe, and a source can be driven

- Status: Accepted (2026-10-16), proto/cymatic-world (the brief's optional coupling: one clear interaction)
- Code: `ResonanceSpec::probe`, `ResonanceSource::drive`, `spatial::resonanceProbes` (`src/spatial/resonance.*`),
  `Composition::publishFieldProbes` (`src/scene/composition.*`, called by `Engine` beside the cinematic signals and
  before every route bind), `field/<name>/resonance/source/<i>/drive` (`src/scene/field_params.cpp`).
- Tests: `tests/unit/test_resonance_field.cpp` ("A resonance reader with a probe publishes its value at that point",
  "A source's drive rings its own modes without the music").

## Context

The brief asks, once the three prototypes work, for one demonstrable coupling between subsystems with the simplest
causal mechanism and no unstable feedback. Every consumer already reads one field, but nothing let one PART of the
field drive another, and the field's state is a pure function of the audio history (ADR-1400), so it cannot carry an
interaction in itself.

## Decision

1. **A probe:** a resonance reader with `"probe": [x, y, z]` publishes its scalar value there each frame as the bus
   signal `field.<name>.probe` (declared before routes bind; read by routes from the next frame).
2. **A drive:** `ResonanceSource::drive` adds excitation straight into every mode the source owns (both envelopes),
   under the source's own competition and limiter.
3. **The limiter and competition are per source:** each source is a body with its own energy budget, so a driven
   body is not silenced by a louder one elsewhere.
4. The coupling is then authored data, a route: Cymatic World's "sympathetic resonance" routes the ground's
   low-mode ringing at the druse's foot (a probe listening only below u = 0.22) into the druse's own source's drive
   (u 0.25-0.6). The bands are disjoint, so the loop cannot close: stable by construction.

## Consequences

- Deterministic and seek-exact up to the route's own smoothing (a one-frame latency, as every bus signal).
- A probe costs one field sample on the CPU a frame.
- A coupling whose probe hears the band it drives would feed back; the route's clamp bounds it, but the author has to
  keep the bands apart (documented in `docs/resonance-field.md`).
