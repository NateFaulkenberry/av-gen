# ADR-1182: A route can normalise its signal to its own running peak

**Status:** Accepted (proto/chorus-field)
**Date:** 2026-10-07
**Implemented by:**
- `ProcessorChain::normalizeSeconds`, `normalizeFloor` and `normalizeSmoothMs`, the stage in
  `ProcessorChain::process`, and `State::level` and `peak` (`src/params/processor.{hpp,cpp}`);
- the JSON in `src/params/serialization.cpp`;
- the route and replay hashes in `src/app/engine.cpp`, `src/scene/composition.cpp` and
  `src/ui/route_row_logic.hpp`;
- the route row's "normalise s" slider in `src/ui/control_panel.cpp`.

**Tests:** `tests/unit/test_route_normalize.cpp` (`[adr1182]`):
- a quieter section is heard fully once the peak has fallen to it, and a chain with the stage off passes it
  through (the control);
- silence is not amplified past the floor;
- a train of kicks reads as one steady level;
- the stage round-trips, is absent when off, and is refused when negative.

## Context

The Chorus Field's god exists in proportion to the bass. Trench's second drop is quieter than its first, so a
fixed gain made the god come back only partly after the breakdown, which reads as a mistake. Hand-tuning per
section would fix one song and break the next, and live input has no sections to tune. Normalising to the
whole track's maximum (as ADR-1116's spectrogram does) fails the same way: one loud moment dims everything
after it.

## Decision

A route's processor chain gains a second stage, after the delay and before gain: an **automatic gain**.

```json
"chain": {"normalizeSeconds": 20.0, "normalizeFloor": 0.08, "normalizeSmoothMs": 800.0, ...}
```

The stage works in three steps:
1. `level` is a one-pole of the signal over `normalizeSmoothMs`. This matters because a kick-heavy band is
   made of transients. Normalised instant by instant, a steady drop reads as mostly 10% of its own kick
   peaks.
2. `peak = max(level, peak · exp(−dt / normalizeSeconds))`. The peak rises at once and falls back with that
   time constant.
3. The stage outputs `level / max(peak, normalizeFloor)`. Gain, curve, clamp, smoothing, spring and
   integrate then shape it as before.

The rest:
- **Off by default** (`normalizeSeconds` 0). The keys are written only when the stage is on, so every
  existing project round-trips byte-identically.
- **Seeking.** The state (`level`, `peak`) is part of `ProcessorChain::State`, so seeks and checkpoints
  replay it exactly like every other chain state (ADR-901, ADR-1168).
- **It is a function of the signal's own history,** so it works unchanged on another song and on live input.

## Consequences

- In the Chorus Field (`examples/chorus-field/p8-god-trench.json`) the face routes, the maw and the camera's
  push, reveal and drift all read the bass normalised over 20 s. The second drop and the climax re-form
  fully.
- **Relative is relative.** A song whose intro bass is steady normalises that intro towards 1 too, so the
  god forms early. `normalizeFloor` is the absolute guard. Set it near the level that should never read as
  "full".
- It costs two floats of state and one exp per route per frame.

## Rejected alternatives

- **A per-section gain table:** song-specific, and blind on live input.
- **Normalising to the track maximum:** one peak dims the rest.
- **A separate auto-gain signal on the bus:** it would be one more reactivity path beside routes (ADR-097).
  The chain is where per-route shaping already lives.
