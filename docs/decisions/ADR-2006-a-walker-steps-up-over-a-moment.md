# ADR-2006: A walker steps up over a moment, and comes down from a step when it stops

- Status: Accepted (Musical Geography, CPD-R13, the owner's note on a possible animation issue on the moving floor,
  2026-11-11), scene/musical-geography. Amends ADR-1997's walk (`Hop` Move phase).
- Code: `kStepLead`, `kStepRise`, `leaveMove`, `carry_` / `kCarryTime` (`src/entity/behaviors.cpp`).
- Tests: `[adr2006]` (with `[adr2005]`): no walking body moves more than 0.5 m vertically between two 60 Hz frames.

## Context

The owner: "lets make sure there is no potential for animation flicker when traversing these tiles ... don't make a
fix unless you find something truly reproducible/diagnosible".

Audit on draft9, the CPD-R6 trace at 60 fps (53 bodies, 210 s, about 650k body-frames):

| Check | Found |
|---|---|
| One-frame activity blips (A, B, A) | none |
| One-frame airborne blips | none |
| One-frame height pops of a **walking** body over 1 m | 358 (up to 3.42 m), every one upward |
| One-frame drops of a body that had just **stopped** (pause, crouch) | about 15 (up to 1.2 m) |

- **The step-up pops** are art's `stepHeight` 3. A walker crossing onto a higher tile rose to it in one frame ("up a
  step at once, never inside the tile").
- **The drops on stopping** happen because a walker easing down a step snapped to the tile when it stopped.

## Decision

- **Stepping up.** A walker reads the ground a quarter-second of its walk ahead and rises toward a higher tile over
  about 0.06 s before its centre crosses the edge. It is never below the tile it is on.
- **Stopping.** The descent a walker was still easing when it stopped is carried into the drawn position, and fades
  over 0.05 s.

## Consequences

Draft9 after:
- walking pops over 1 m: 358 -> 2 (the largest 1.16 m, against 3.42 m before);
- frames over 0.3 m: 1,450 -> about 1,100. What remains is the floor itself under a moving body: ring fronts move a
  tile about 0.3 m in a frame.

Not fixed, and not the bodies': one tile under a landed body (body 4 at 18.58 s) drops 1.23 m in a frame. That is the
ground's own motion (a ring front), drawn the same with or without a body.
