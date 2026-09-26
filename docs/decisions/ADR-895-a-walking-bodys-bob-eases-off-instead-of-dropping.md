# ADR-895: A walking body's bob eases off instead of dropping

**Status:** Accepted
**Date:** 2026-09-25
**Follows:** ADR-198 (secondary motion), ADR-830 (the foot-jump benchmark)
**Implemented by:** `Liveliness` (`src/entity/behaviors.cpp`)
**Tests:** ADR-830's "no alien's foot jumps in the first 40 s of the multicam film", which now passes on
every route it was tried on. Re-pinned: ADR-623's default-off trace digest (below).

## Context

ADR-894 changed where Glowmere's water is, and the aliens are drawn to water, so Ember took a
different route. ADR-830's benchmark then failed: **Ember's feet jumped 0.19 m between two posed
frames at 18.43 s.**

It was not the terrain. A 1 cm probe along Ember's path found no step larger than 2 mm. Diagnostics
in the benchmark showed:

- **The simulated position was smooth** (7.041 → 7.038 m).
- **The drawn root dropped 0.317 m** in one posed frame.
- **The turn rate** went from 0 to −2.45 rad/s in that same frame.
- **The additive motion offset,** `Liveliness`'s stride bob, fell from 0.31 m to 0.008 m in one
  1/60 s step.

The bob is scaled by the body's speed. A decider that wants a sharp turn drops its intended speed to
almost nothing in a single step, and the body's *measured* travel stops dead with it: 0.071 m/s
measured against 0.142 intended. So the bob fell from its crest to the ground in one frame, and the
foot IK absorbed it as a jump in both feet.

On main's route Ember never makes such a turn in the first 40 s. Every other route tried, three of
them, did make one, and each failed the benchmark at 0.17–0.20 m.

## Decision

The bob's amplitude eases toward its target with a 250 ms settle (`kBobSettleMs`, via the file's
existing `lerpRate`). It is a member of the behaviour, so checkpoints carry it (ADR-700 copies
behaviours whole), and `reset` zeroes it.

The body stopping dead is a locomotion question, and this does not answer it. The bob letting a drawn
body fall its whole height in a frame is `Liveliness`'s question, and a quarter-second settle is how
a real body comes off a stride.

## Consequences

- **The benchmark passes** on main's route and on all three re-routed ones.
- **Every alien's worst foot step is at or below main's:**

  | Alien | Worst step now | Main |
  |---|---|---|
  | Ember | 0.048–0.054 m | 0.059 m |
  | Rook | 0.057 m | 0.061 m |
  | Sage | 0.030–0.051 m | 0.043 m |
  | Tide | 0.045 m | 0.046 m |
  | Vane | 0.105 m | 0.105 m |

- **A body starting to walk** bobs in over a quarter of a second rather than at once.
- **ADR-623's pinned trace moved,** as ADR-830's change moved it before. "With the key absent,
  Glowmere behaves exactly as it did before ADR-623" hashes Glowmere's first three seconds,
  including every rig's pose, and a bob that eases in changes every walking rig from its first
  step. ADR-893 and 894 also move the cast.
  - **Re-pinned:** `0xd53e94d7c2073392` → `0x6e863d80f8c6146f`, with the reason in the test.
  - **Why that is safe:** no matcher code changed, the scene carries no key, and the control arm
    still shows the key changing the trace (`0x5ae3252bd3aa1e64` with it on for Rook).
- **Open:** a sharp turn still stops a body's travel dead in one step. That is ADR-830's question
  for the motion stack, recorded rather than answered.
