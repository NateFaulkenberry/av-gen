# ADR-908: Bodies walk through their turns, pivot only from rest, and step round a pivot

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-096 (gait), ADR-213 and ADR-622 (the farm pack's frozen idle), ADR-620 (authored
acceleration), ADR-907 (the forward cone this relies on)
**Implemented by:** `TurnSettings`, `turnPace`, `insideTurn`, `turnCap`; the `GaitSettings` fields `turnRate`,
`turnRadius` and `pivotRadius`, and their JSON keys; `Gait::playbackRate`'s pivot branch (all in
`src/entity/gait.{hpp,cpp}` and `src/entity/action.cpp`). `Wander`'s turn (`src/entity/behaviors.cpp`). The action
tier's `move` and `face` verbs, and `ActionQueue::begin` (`src/entity/action.cpp`).
**Tests:** `tests/unit/test_walk_through_turns.cpp` (five cases, each with a control arm);
`tests/unit/test_character_controls_reach.cpp` (the gait's turn knobs moved as parameters move the body,
and where each control is found)

## Context

Every mover in the engine turned toward its heading at a fixed rate and travelled at
`max(0, cos(heading error))` of its pace. So any turn over 90 degrees began with a dead stop and a
pivot on the spot:

- `Wander` behaviors.cpp:835;
- `Explore` behaviors.cpp:1638;
- the escape behaviors.cpp:162;
- the action tier action.cpp:795, whose turn rate was a hard-coded 2.45 rad/s.

The GV3 audit measured the result:
- The animals did 27–44% of all their turning below 0.3 m/s.
- Every alien pivoted at exactly 141 degrees per second.
- The median turn radius while moving was 0.7–1.4 m, less than a body length.

The owner (§12): "Animals currently appear to rotate around an axis when changing direction. That
reads as a technical animation artifact."

The farm animals made it worse. They ship one clip, `Walk`, and `idleRate` 0 freezes it while a body
stands (ADR-213). So every pivot rotated a frozen mid-stride pose: a statue on a turntable.

## Decision

**A turn radius.** `GaitSettings::turnRadius` covers the action tier, and `wander` has its own
`turnRadius`, as it already had its own `turnRate`. It is one rule, written once in `gait.cpp` as three
questions a mover asks each step:

1. **How much of its pace does it keep** (`turnPace`)?
   - At rest: the cosine of what is left to turn. From rest it pivots until the way is within 90
     degrees, then walks out of the turn.
   - Moving: never less than half its pace (`kTurnKeep`). It slows into a sharp turn and never stops
     for one.
2. **Can it get there at all** (`insideTurn`)? A point `d` metres away and `e` radians off the heading
   is inside the circle the body turns on when `d < 2 R |sin e|`. Walking on, the body would orbit it.
   - A wander's destination was only ever somewhere to walk toward, so it counts as reached.
   - An errand's target is the point of the errand, so the mover brakes and pivots once, from rest.
3. **How fast may it turn** (`turnCap`)?
   - A body that will be at rest this step may pivot at `turnRate`.
   - A moving body turns no faster than the slower of its two paces (the one it had and the one it
     now has) over the radius, which is exactly what a circle of that radius is. So it traces a turn
     at least `R` wide whether speeding up or slowing down.

The pace is decided first and the turn after, so a body leaving rest toward a way within 90 degrees is
already moving on the step it leaves, and walks out on its circle rather than pivoting a sliver first.

**A turn rate for the action tier.** `GaitSettings::turnRate` (degrees per second, like every other
`turnRate` in the vocabulary) replaces the `move` verb's hard-coded 2.45 rad/s. The `face` verb reads it
too, before its own 2.5 rad/s default. It is 0 when not authored: each verb keeps its old default, so
with no radius the old mover is the old mover to the bit.

**A walk-through `move` starts at the pace the body already has.** `ActionQueue::begin` primes the
action's eased speed from the body's current speed. The old mover started every `move` from rest,
which cost it nothing because it pivoted first anyway. For a mover that turns on a circle it is the
whole difference: a decider changing its mind mid-walk would otherwise stop the body dead, count it as
at rest, and pivot. Two other small changes, both only where a radius is set:
- The stuck clock waits while the body is turning back on its circle, which walks away from the goal
  before it walks toward it.
- The steering fan looks at least one and a half radii ahead.

**Wander's radius defaults to its own speed over its own turn rate**: the tightest turn it could make
at full pace. So a wanderer nobody tuned still walks through its turns and never stops to pivot out of
one. An author who wants a wider, heavier turn says how wide.

**A body with no idle clip plays its cycle while it turns on the spot.** For a `Turn` with
`idleRate` below `kVisibleClipRate` (the asset has no idle), the gait plays the locomotion cycle at
the speed the feet move round the pivot: `|turnRate| * pivotRadius / walkSpeed`. It is floored where a
cycle becomes visible and capped at the gait's ceiling. `pivotRadius` defaults to 1 m, about half a
farm animal at the Glowmere cast scale. An asset with a real idle has its own turn clip and is left
alone.

**The turn knobs are parameters**, for a body that authors a gait: `entity/<name>/gait/turnRate`
("turn rate on errands (degrees a second, 0 = the default 140)"), `gait/turnRadius` ("turn radius on
errands (m, 0 = turns on the spot)") and `gait/pivotRadius` ("turning on the spot, its feet circle at
(m)"). `Entity::refreshGait` reads them back at the top of every step on both paths into `gaitLive_`,
which is what the action tier and the clip rate are handed; `desc_.gait` stays the authored one, so a
save writes what the author wrote. `gaitLive_` starts as the authored gait in the constructor, because a
seek can publish a clip rate for a body it never stepped. Wander's `turnRadius` and `turnRate` are its
own parameters. All of them are found in the Parameters panel (`entity` → `<name>/gait`,
`<name>/wander`) and in the Inspector's character section (ADR-907 §8).

**On defaults.**
- **`wander` walks through its turns by default,** for ADR-907's reason: the pattern is built in and
  nothing in the character lab routes through `wander`.
- **The action tier's `turnRadius` is opt-in** (0). This is deliberate:
  - A turning circle is a property of a body's size, and the right circle for a 3.5 m alien is not
    the right one for the lab's 1.8 m walkers.
  - The Character Intelligence Lab's decided fixtures (the guard post, the pasture, the autonomy demo,
    the river crossing) route bodies tightly round rocks and along river banks. Their benchmarks
    assert those routes, and a default circle would change every one of them.
  - The aliens' reversals in GV3 are decision patterns (ADR-909), not mover patterns.
  - ADR-620 set the same precedent: a limit on the behaviour tier is honoured only where authored.
- **The single-clip pivot is on by default:** a frozen stride rotating on the spot is a defect in any
  scene.

## Consequences

- **A 3 m circle holds.** A wanderer given one turned no tighter than 3.00 m on nine turns in ten, and
  turned 4 of its 1,641 degrees standing (0.2%). With a 0.4 m circle, the same body turned as tight as
  0.47 m.
- **An errand turned back mid-walk walks a half circle.** A body at pace told to go behind it swung
  exactly 5.00 m (the diameter), turned nothing below 0.3 m/s, and made one stop, the arrival. The old
  mover in the same errand braked, turned 103 degrees below 0.3 m/s and made two stops.
- **The authored rate is the verbs' rate.** A quarter turn took 1.95 s at an authored 45 deg/s, and
  0.63 s at the `face` verb's own 2.5 rad/s. A `move` pivoted at exactly 60 deg/s when authored and at
  140 deg/s (the old constant) when not.
- **Farm animals turning on the spot now step round.** Every frame the gait calls a turn plays the
  cycle, where before every one was frozen.
- **The legacy mover is kept, and not as a compatibility shim.** It is a body with no turning circle,
  a real choice for a small walker in a tight space, and the default for the action tier for the
  reasons above.
- **Every scene with a `wander` behaviour turns differently** (ADR-907's list). Every farm body turning
  on the spot plays its clip, in every scene. Decided and action-driven bodies with no `turnRadius` are
  unchanged: the old `move` path is bit-identical, and the `face` verb reads the gait's rate only when
  it is authored.
- **`explore` still pivots.** Its golden route trace (ADR-333) is untouched, and GV3 does not use it.
- **Open:** consecutive `move` actions still brake to a stop at every leg's end, because each leg
  slows into its own goal. So the route considerer's multi-leg errands stop at each waypoint. GV3's
  aliens walk single-`move` errands, so the film is not affected. Recorded, not fixed here.
