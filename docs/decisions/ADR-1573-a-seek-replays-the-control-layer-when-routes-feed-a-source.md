# ADR-1573: A seek replays the control layer when routes feed a source

- Status: Accepted (art/rainbow-world-2)
- Amends ADR-1168 (the control layer, replayed on a seek) and ADR-901. Additive: a project with neither scene states
  nor a route into a source parameter seeks exactly as before.
- Code: `src/app/engine.{hpp,cpp}`:
  - `Engine::controlReplayNeeded()`;
  - the seek's condition for `replayControl`;
  - `replayControl` keeps the source parameters' finals from the frame before the target and restores them for the
    landing frame.
- Tests: `tests/integration/test_macro_seek.cpp` (`[adr1573]`).
  - A stateful route (an onset into a linear-fall envelope) writes a macro knob, and a stateless route reads
    `macro.<k>`.
  - At three instants, the seek's landing frame and the next two equal a play's, bit for bit.
  - Some instant in the run is mid-envelope.
  - A project with no route into a source does not pay for the replay.

## Problem

A macro source publishes `macro.<k>` from its knob `macros/<k>`. That is the natural "musical control" layer: stem
signals into a few conditioned knobs, the knobs fanned out to many targets. Rainbow World 2 uses it for its swell,
synth light, lift and world scan.

Only the control replay steps the sources, and the seek ran it only for projects with scene states. On every other
project:
- the landing frame of a seek published the knob's reset value;
- everything downstream of the macro was a frame wrong;
- a world scan latched by a chain on the knob was missing altogether.

This was measured as a render started at 49.9 s against a render through it: the first frame had no scan.

Fixing that exposed a second, one-frame error. A source publishes its knob a frame late: it reads the finals the last
frame's routes wrote. The replay ran through the target frame itself, so the landing frame published the target's
value where a play publishes the frame before's. The test caught it as a landing value 1/60 of an envelope ahead.

## Decision

- **When to replay:** `controlReplayNeeded()` is true for scene states, as before, or when any enabled route writes a
  path under `macros/` or `sources/`. Either one makes the seek run `replayControl`.
- **The landing frame:** `replayControl` remembers the source parameters' finals after the frame before the target,
  and restores them after its last step. The landing frame's sources then publish what a play's would have.

## Consequences

- A seek in such a project costs the control replay, from zero to the target, on the play's frame grid (ADR-1168's
  cost). Rainbow World's showcase replays about 250 routes.
- Interactive scrubs of these projects pay it too, as scene-state projects already did.
