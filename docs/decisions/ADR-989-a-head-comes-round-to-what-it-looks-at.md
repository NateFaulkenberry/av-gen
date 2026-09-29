# ADR-989: A head comes round to what it looks at

**Status:** Accepted
**Date:** 2026-09-29
**Found by:** the GV3 art pass, revision round 1 (item 7, relayed by the coordinator): the aliens "when they turn
their heads its not a smooth animation currently, more a of snap into possition"
**Implemented by:** `GazeSettings` (`EntityDesc::gaze`), `Entity::advanceGaze`, `Entity::publishedLookTarget` and the
`gaze` entity key in `src/entity/entity.{hpp,cpp}`; GV3's aliens in `tools/gv3/cast.py` (`ALIEN_GAZE`)
**Tests:** `[adr989]` in `tests/unit/test_entity_action.cpp` (without a gaze the aim jumps -- the control; with one
it comes round within its rate and settles on the subject; a seek lands it where a play does; a subject crossing
the body's back does not throw the head to the other shoulder); `avgen_foot_probe --feet-out` (the head's rotation
in the world, every frame)

## Context

A look layer (ADR-300) aims the head at `LocomotionState::lookTarget` on every frame, and the entity tier publishes
there whatever the body attends to. Attention moves on in a step -- a decider's focus changes, an action names a
new point -- so the aim moved with it: a quarter turn in one posed frame. The layer's weight is eased in and out
(Phase B §46), which covers a look starting and stopping, not a look moving from one subject to the next.

And the layer clamps the aim to `maxYaw` either side of the body's facing, by azimuth: a subject crossing behind the
body moved the clamped aim from one limit to the other at once -- 150 degrees in one posed frame, the largest snaps.

Measured on GV3's film, the aliens' head joint in the world between posed frames (33,000 of them, 30 Hz): median
18 degrees a second, 99th percentile 671, worst 4,587; 405 steps faster than 400 degrees a second, 311 faster than
800; angular acceleration at the 99th percentile 61,800 degrees a second squared.

## Decision

1. **A gaze** (`"gaze": {"settle", "maxTurnRate", "eyeHeight", "maxYaw"}` on an entity; absent, or `settle` 0, is
   every scene before it). The direction the head is aimed along -- a yaw and a pitch in the world, from the body's
   eyes -- is a critically damped spring toward the subject's direction, 95% of the way round in `settle` seconds,
   integrated in closed form so a step of any length lands where the spring would, and never turning faster than
   `maxTurnRate`. It is kept within `maxYaw` of the body's facing, a subject further round looked at from the limit
   on its own side -- and one within 30 degrees of straight behind from the side the gaze is already on. With no
   subject the gaze comes back to the body's facing, level, so the next subject is turned to from where the head
   is. The subject is where it settles.
2. **It is state in the entity tier, which may remember** (`LocomotionState`'s own note: `EntityWorld::seek`
   replays every step). `advanceGaze` runs on both publish paths, after `advanceMotion` (ADR-554's rule); the gaze
   is a member of the entity, and a checkpoint copies every entity whole (ADR-700). So a seek lands the gaze
   exactly where a play of the same steps does -- measured mid-turn, to 1e-4 m. What the pose tier is handed is a
   point on the gaze at the subject's distance (`publishedLookTarget`); the layer does what it always did.
3. **GV3's aliens**: settle 0.35 s, at most 200 degrees a second, from 2.6 m (the head joint stands 2.45 m up and
   the eyes 2.69 at the cast's 1.94x), within 70 degrees of the body's facing -- inside their look layer's 75, so
   its clamp never answers. The body's own turn (100 degrees a second) follows the head.

## Consequences

The same measurement, after:

| | before | after |
|---|---|---|
| head speed, median / 99th percentile / worst (deg/s) | 18 / 671 / 4,587 | 21 / 211 / 1,653 |
| steps over 400 deg/s (over 800) | 405 (311) | 28 (6) |
| angular acceleration, 99th percentile / worst (deg/s^2) | 61,800 / 151,500 | 3,050 / 56,200 |

Of the 28 left, 15 are Sage's `startle` (the authored `Fight_head_hit` reaction to a beam, a flinch by design, and
all six over 800), four are Sage's walking turn at 28.7 s (420-466 degrees a second for four posed frames: the
look's weight blending while body and gaze both turn), and the other nine are the film's first 0.17 s, where every
alien's look layer blends in from the first frame.

- **Nothing else moves:** every body travels exactly as before, the aliens' feet measure the same (ADR-988's table,
  to the millimetre), and the drummer's whole pose through his abduction (168-200 s, 54 joints, every frame) is
  bit-identical. GV2's fingerprint (ADR-623's trace) is 0x7e0ba810e539e375: no GV2 body has a gaze.
- The look layer's own weight ease is unchanged, and so is its clamp: past 70 degrees GV3's aliens never reach it.
- A scrub reconstructs the gaze rather than restarting it, which a pose-tier ease could not have done.

## Rejected alternatives

- **Easing the aim in the pose tier:** the head's rotation would have to be remembered from the frame before, and a
  scrub poses once -- the head would land on the subject on a scrubbed frame and lag it on the played one.
- **Easing the published point** (not the direction): a subject twice as far moves the point twice as fast for the
  same turn, and a point swept between two subjects passes through the space between them rather than round.
- **A longer blend of the layer's weight on every change of subject:** the head would drop toward the clip's
  forward and come back up, a nod, on every glance.
- **Letting the neck share the turn:** the aim layer turns its joints about one pivot, the head; a neck turned about
  the head's pivot is not a neck turning.
