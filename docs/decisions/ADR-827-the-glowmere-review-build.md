# ADR-827: The Glowmere review build: what changed in the multicam film, and the arms it is judged by

**Status:** Accepted
**Date:** 2026-09-25
**Related:** ADR-821 (clip semantics), ADR-822 (beat hops), ADR-825 (baked matcher database), ADR-826
(character quality analyzer); Phase B §66, Phase C §86, Phase D §66–§69; the owner's rulings of
2026-09-25
**Implemented by:**
- the entity sink's measured loop;
- `examples/world/glowmere-valley-2-multicam.scene.json`: beam radius 150 m and `proceduralMotion`
  on the five aliens;
- `tools/make_review_arms.py`;
- `tools/make_review_follow.py`;
- `tools/make_scout_motion_db.sh`.

**Report:** `docs/reports/glowmere-review-build.md`

## Context

The owner said yes to the review build (the board's ★ items) on the multicam film, and ruled on the
motion-stack decisions:
- react HOLD;
- keep the beat hops;
- `proceduralMotion` on in the film;
- the matcher reviewed as an arm once its loading path was built.

## Decision

1. **Autonomous clips play as they measure.**
   - The entity sink passes each clip's measured loop (ADR-821) as the play's override. `Crazy`
     (react) and `Landing` play once and hold their last frame, and every clip that closes loops as
     before.
   - Measured over the film's 226 s: no alien rig ever plays a one-shot clip in the multicam film.
     Its pixels are therefore unchanged by this rule.
   - The rule shows wherever a react or a landing plays; review sheet 01 is its before/after.
2. **The beam reaches 150 m.**
   - The abduction beam fires 12 times, 55–140 m from the aliens, and its world event reached 60 m.
     Across the whole film, one alien reacted once.
   - At 150 m the five react by their own personalities and rates (item ★.5, staggered). At the
     44.2 s beam: Ember approaches at +0.03 s, Vane +0.06 s, Rook +0.76 s, and Sage flees at
     +0.86 s. Bold Rook approaches at most beams, cautious Sage flees at most, and Tide rarely
     reacts.
3. **`proceduralMotion` is on for the five aliens.** They are posed through the Phase B provider
   chain (item ★.2's motion half). The matcher stays off in the film and is reviewed as an arm.
4. **The arms,** each the film with one variable changed:
   - AI ON (the film);
   - AI OFF (the pre-awareness behaviours);
   - procedural;
   - matcher (procedural plus the baked scout database).

   Each arm is judged by:
   - the analyzer (ADR-826), which measures the root;
   - renders with a camera welded to a body, for the pose.

   Renders start at 0 s at the film's own 60 fps. Both a mid-film start and a different frame rate
   simulate a different run: both mistakes were made while building this, and both lost the motion
   under review.

## Consequences

- The multicam film changes: the aliens react to the saucer, and they are posed through the
  provider chain. It is the Director's benchmark film, so the Director's benchmark tests are rerun
  on this branch.
- The analyzer cannot tell the procedural and matcher arms from AI ON, because it measures the root
  and those arms change only the pose. Their review is by eye (sheets 05 and 06), and they are
  confirmed posed through the path they claim: `MotionDebug` reports `posedByProvider`, and for the
  matcher arm `matching` on 1,738 samples.
