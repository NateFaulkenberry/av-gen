# ADR-766: A directed performance is orders at seconds, carried out by the character

**Status:** Accepted
**Date:** 2026-09-25
**Related:**
- ADR-763 (live until recorded) and ADR-765 (recording on request);
- the Motion lead's ADR-824 (scheduled direction replayed exactly) and ADR-828 (goals);
- ADR-098 (tiers).

**Implemented by:**
- `directedBeat` and `DirectedVerb` in `src/directing/performance.*`;
- `validateDirectedPerformance` in `validator.cpp`;
- the directed branch of `compilePlan`;
- the recorder, which now also records directed performances (`ordersTailSeconds`).

**Tests:** `tests/unit/test_directing_directed.cpp` (`[directing][directed]`)

## Decision

- **Each beat is one order.** A beat in a `directed` performance compiles to one scheduled
  `EntityAction` sequence event at its second. ADR-824 applies that event inside the simulation,
  on a play and on a scrub alike. The vocabulary maps onto the engine's section verbs:

  | Beat | Engine verb |
  |---|---|
  | `face` a character | `face` |
  | `approach` a character | `move` (EntityRef) |
  | `go_to` a place | `goal`: a place is a landmark, not an entity, and a goal is how the engine walks a body to one |
  | `interact` with `prop.verb` | `interact` |
  | `release` | hands the body back to its autonomy |
  | any activity on the character's card | `pose` |

- **What the validator enforces:**
  - Every order needs a time. After the first, when the previous order finishes is up to the
    character, not a plan-time fact.
  - `face` and `approach` must target characters; places are refused, with "go_to a place instead".
  - `go_to` needs the character's goal slot.
  - Only a `go_to` may `emit`: its `goal.arrived` / `goal.done` is the only event an order raises.
- **Orders are live.** How the body carries an order out belongs to the simulation, so directed
  performances compile only in live-tier plans. A baked plan refuses them. Cues on their events and
  slow motion wait for a recording, exactly as for goals (ADR-763). Recording bakes a directed
  performance the same way: a fixed tail after its last order, since orders raise no events.

## Consequences

- **Played on the benchmark.** Rook is ordered at 20 s to face Vane, at 22 s to react, and at 26 s
  released. Each result is compared against the same world without the orders:

  | Measurement | With the orders | Without |
  |---|---|---|
  | Closest Rook comes to facing Vane between the orders | 0.038 rad | 2.14 rad |
  | Rook's clip at 23 s | `Crazy`, from 22.047 s (the react order's second) | `Idle` |

  - A scrub to 24 s lands 0 m from the play.
  - Recording bakes the performance: 121 keys and 8 clip cues. It plays back 0.0000 m from the
    recording, and a scrub lands 0.0000 m from the play.
- **Proven red:** moving the orders 30 s later fails the facing and react checks.
- **Found while testing:** at 5–7 s Rook already faces Tide and plays `Crazy` on his own ("react now
  holds"). The first version of this test passed on that autonomy alone. The test now proves the
  effect by comparing against the unordered world.
