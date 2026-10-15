# ADR-1372: A sine deformer's phase is unbounded and wrapped

- Status: Accepted (2026-10-15), art/digital-ocean-2 (Digital Ocean's anime water, Pass 3: the wave-speed control)
- Code: `registerProceduralParameters` and `applyProceduralParameters` (`src/scene/procedural.cpp`).
- Tests: `tests/unit/test_procedural_params.cpp` ("A deformer's phase takes a value far beyond one turn and is wrapped
  to an angle").

## Context

A sine deformer moves its wave by `sin(k x + phase + speed t)`. Routing a control onto `speed` makes the wave jump: a
change of speed by `ds` at time `t` moves every crest by `t ds` radians at once, 30 radians for a modest change five
minutes into a set. A performer's "wave speed" control cannot be built on it.

The route chain already has the right stage. `integrate` (ADR-1041) outputs the running integral of its input, so a
rate becomes a phase, and the phase never jumps whatever the rate does. Routed onto `deform/<n>/phase` with the
wave's angular speed as the amount, it adds `w * integral(rate dt)`: the waves speed up and slow down smoothly.

Two things stopped it. The `phase` parameter's hard range was +-100 radians, so an integrated phase clamped after a
minute or two and the wave froze. And a phase that grows without bound loses precision in the shader's f32 sine.

## Decision

- The `phase` parameter's hard range is +-1e6 radians. Its soft range (the slider) stays one turn either way.
- `applyProceduralParameters` wraps the phase to (-pi, pi] (`std::remainder`, in double) when it copies the final.
  Only the sine deformer reads `phase`, and to a sine a phase is an angle, so the wrap changes no image. The CPU
  evaluator and the GPU packing both read the wrapped value.

## Consequences

- A scene can give a wave a performer-controlled speed: keep the authored `speed` (pure in time, so seeks are exact)
  and add an integrated route for the change. Digital Ocean does this for its wave-speed control (CC 9) and for a
  slight quickening with the music's level.
- Integrated routes on live input (control, MIDI) are not replayed by a seek (ADR-901), so after a seek the added
  phase restarts from zero. The waves keep their authored motion; only the performer's accumulated offset is lost.
  Offline, a route on a pure-in-time source (a REVIEW timeline, `audio.*`) is replayed and lands exactly.
- No existing scene sets a phase beyond +-100, so nothing else changes.
