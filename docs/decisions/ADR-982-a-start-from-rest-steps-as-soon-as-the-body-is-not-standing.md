# ADR-982: A start from rest steps as soon as the body is not standing

**Status:** Accepted
**Date:** 2026-09-28
**Found by:** the GV3 targeted art pass, item 3 (`docs/glowmere-valley-3/art-pass/00-brief.md`): "a small amount of
visible sliding before the first actual footstep ... most noticeable when a character is idle and begins moving"
**Amends:** ADR-096 (the gait's hysteresis and dwell)
**Implemented by:** `entity::Gait::select`; the measuring tool `tools/foot_probe.cpp` (`avgen_foot_probe`)
**Tests:** `tests/unit/test_entity_action.cpp` (`[adr982]`), with the existing `[gait]` cases

## Context

Nothing measured a drawn foot, so a probe was built: `avgen_foot_probe` runs the offline engine over a project,
posing every rig every frame, reads each character's feet from the rig's evaluated pose through the skinned
mesh's world transform, and reports, at every start from rest, how far the feet travel over the ground while the
gait still plays a standing clip.

On Glowmere Valley 3 the aliens' 81 starts slid a median 0.256 m per foot (p90 0.88 m, worst 1.34 m) over a
median 0.37 s of `Idle` or `Idle_turn`, for a 3.5 m body. Frame by frame (Rook at 2.43 s): the mover
accelerates at 4.8 m/s^2; the gait holds `Idle` until the speed passes `moveEnter` (0.90 m/s, which each alien's
scene sets at 0.1875 s of its own acceleration); below it, turning, it picks `Idle_turn`; and the dwell
(`minDwell`) then refuses the walk. The body reaches 1.85 m/s and 0.35 m of travel before `Walking` begins,
both feet planted by the standing clip the whole way.

The hysteresis band exists for a body whose speed sits on the boundary. A body setting off from rest is not
one: it crosses the band once, upward, and every frame inside it is a standing clip over moving ground.

## Decision

A standing gait (Idle or Turn) that has come to rest (speed at or under `kTurnRestSpeed`) since it last stepped
enters the stepping gait as soon as the body's speed exceeds `moveExit` -- the speed below which the body counts
as standing by its own definition -- and the dwell does not hold that one change back. Everything else is
unchanged: a body that has not been at rest since it last stepped, however its speed jitters about the band,
meets the band and the dwell exactly as before.

## Consequences

- Measured over the whole film, the same 81 alien starts: the standing-clip slide per foot goes from median
  0.256 m / p90 0.880 m / max 1.344 m to **median 0.018 m / p90 0.022 m / max 0.082 m**, and the standing
  seconds from median 0.37 s to 0.083 s.
- **The simulation does not move.** The gait chooses what is played, not where a body goes: every character's
  travelled distance and every start's instant are bit-identical before and after, so the cast trace, the
  framing and every shot that follows a character are unchanged.
- A body creeping at a speed between `moveExit` and `moveEnter` after a start now walks slowly, rate matched,
  instead of standing in `Idle` while it glides.
- The farm animals are unchanged: their one clip is their idle (ADR-213), played at the matched rate from the
  first frame. What remains of their start slide (median 0, p90 0.12 m per hoof before the first step over 210
  starts; 1.9 cm per hoof per metre over the whole film) is the pack's own stance, whose speed varies six-fold
  within one stance (`avgen_foot_probe --profile cow-12`), which rate matching cannot follow. Holding a hoof
  would take foot locking (the §14/§15 `footLock` over contact tracks) on four-legged rigs -- a locomotion
  change, recorded for the owner rather than made here.

**Amended the same day, after the full CPU suite.** `test_stride_warp`'s ADR-829 case -- GV2's Rook turns on
the spot at 3.03 s and walks off at 3.30 s -- measured a foot moving 0.128 model units in one posed frame
(its bound is 0.06; a start from a stand moves one about 0.04): the rule handed Rook his walk while he was still
pivoting. So a start is taken from a turn on the spot only once the turn is over (`|turnRate| <= turnEnter`);
from `Idle` as before. Re-measured on the art pass's GV3 (90 alien starts, the cast now walking round the new
heroes and the performers): median 0.018 m, p90 0.023 m, max 0.185 m per foot, standing seconds median 0.083 s
-- the medians as above; the worst is a turn that now finishes before its walk. And GV2's trace changed with the
rule, as it had to: `test_motion_matching_default_off`'s digest is re-pinned with the reason, after a diagnostic
build with the rule off gave back the old digest exactly.

## Found on the way, not changed

The ground layers plant a foot at its REST height above the ground whenever the body is grounded
(`plantOnPlane(..., groundOffset + restTipHeight)`, weight = the grounded bit), so in the drawn pose an alien's
swing foot does not lift: Rook's feet hold 0.267 m above the terrain through `Walking`, whose own swing lifts
them about 0.24 m. That is the swing flattened, on every character with ground layers in every scene; it is a
foot-IK change with that reach, and is recorded for the owner rather than made in an art pass.

## Rejected alternatives

- **Lowering `moveEnter` in Glowmere Valley 3's data.** It narrows the band for every body and every stop, not
  just the start, and it leaves the dwell refusing a start that follows a stop.
- **Root motion for the first step.** The right tool for a body that must not move until its foot does, and a
  change to the locomotion system the brief rules out.

## Revisit triggers

- A body that starts, stops and starts again faster than the eye can follow a clip change: it now changes clip
  on each start.
- Foot locking arriving for quadrupeds (their start slide is then the stance, not the gait).
