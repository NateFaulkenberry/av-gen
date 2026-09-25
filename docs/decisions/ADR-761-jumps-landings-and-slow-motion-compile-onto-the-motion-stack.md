# ADR-761: Jumps, landings and slow motion compile onto the motion stack

**Status:** Accepted
**Date:** 2026-09-25
**Related:** ADR-758/759 (performances), ADR-820 (clip ownership, entry blend), ADR-821 (clip
semantics), ADR-822 (the jump arc), ADR-823 (local retime); spec §26–§33, §57
**Implemented by:**
- `compilePerformance` (`jump`, `land`, `JumpOutcome`) in `src/directing/performance.*`;
- the jump, retime, `moment` and `entrySeconds` rules in `validatePlan`;
- the retime pass in `compilePlan`;
- `JumpEnvelope` and `ActivityCapability::semantics` in `src/directing/capabilities.*`;
- `SceneFacts::groundAt`.

**Tests:**
- `tests/unit/test_directing_airborne.cpp`;
- the `entrySeconds` case in `test_directing_performance.cpp`;
- the `rook_jump` and `rook_hop` golden plans.

## Decision

- **A `jump` beat flies the engine's one arc.** It uses `entity::planJump`, the same arc an
  autonomous hop flies.
  - The take-off is where the body is, on the ground (`SceneFacts::groundAt`: the surface the
    performer stands on).
  - **Over a target,** the landing is as far beyond the target's centre as the take-off was before
    it. The apex is the larger of the character's hop and `entity::minimumApex` for the target's
    footprint plus clearance.
  - **Without a target,** it is a hop along the line of travel. It goes as far as the arrival speed
    carries the body through its own flight, up to its `maxDistance`.
  - **What is compiled:**
    - `seq::jumpKeys` (Linear keys: M1's `Smooth` easing disagrees with the bake);
    - an `airborne` span, so the performer keeps the arc's height rather than the terrain's;
    - the jump clip cued `once` and fitted to the arc by its measured takeoff and touchdown
      (`seq::jumpClipCue`). Both scout jump clips measure as loops, so `once` is written rather
      than inferred.
- **`emits` on a jump names one moment:** `moment: takeoff | peak | touchdown`, with `peak` as the
  default. The time comes from `seq::jumpTimes`, so a cue on `rook.jump_peak` fires at the arc's
  apex.
- **A `land` beat** plays the landing clip once, then hands back to the gait. The body holds for
  the character's `landSeconds` (or the beat's `seconds`).
- **The validator checks the arc the compiler would fly.** It runs the same `compilePerformance`
  and reads its `JumpOutcome`, so a refusal is about the exact geometry that would have been
  compiled. It reports `SPATIAL_INFEASIBLE` when:
  - the apex is above the character's `maxApex`;
  - the leap is longer than `maxDistance`;
  - the arc meets the ground early (`entity::checkArc`);
  - there is no terrain.

  The height-only check stays first, and is what the benchmark reports. It compares
  obstacle + clearance against `maxApex` (ADR-822), not the hop apex.
  - Per the owner: Rook stays at 1.1 m, and the Umbra leap is refused as "needs 5.75 m" against
    1.10 m.
  - Suggested instead: a character with a larger jump, or a different path.
- **A retime is `seq::retimeActor` on one performance's actor** (keys, clip cues, airborne spans),
  applied in time order.
  - The plan events it raises are mapped with `seq::retimeMap`, so markers and the cues on them
    stay on the moment.
  - The film's clock is not touched (spec §33). Frame echo remains an ordinary cue.
  - A retime is refused when it:
    - overlaps another on the same performance;
    - misses the performance;
    - depends on a blocked performance.
- **`entrySeconds`** (ADR-820) is a performance field.
  - A value above 0 blends from wherever the simulation left the body, which is live state.
  - A baked plan therefore refuses it as `NON_DETERMINISTIC`, and a directed plan is warned.
- **The capability card reads the data.**
  - The jump envelope comes from the entity's `jump` block: apex, `maxApex`, gravity,
    `maxDistance` and `landSeconds`, with source `jump` or `default`.
  - Every available activity carries its measured clip semantics: whether it loops, its ground
    kind, and its events.

## Consequences

- A jump, its clip and its peak marker are one plan-time computation. They bake, scrub and render
  the same way.
- `fall` is still `UNSUPPORTED`, because a plan cannot place a drop yet.
- Acrobatics (`backflip`) stay `CAPABILITY_UNAVAILABLE` until an asset exists.
