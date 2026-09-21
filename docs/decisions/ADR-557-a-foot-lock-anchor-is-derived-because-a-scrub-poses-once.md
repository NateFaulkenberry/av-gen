# ADR-557: A foot lock anchor is derived, not remembered, because a scrub poses the rig once

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-086 (a state stores when it was entered), ADR-273 (the seek budget), ADR-359 (the
ground is a plane and the smoothing lives in the entity), ADR-360 (the determinism contract),
ADR-541/ADR-556 (a provider's memory is the entity's), ADR-554 (a seam published from two places)
**Implemented by:** `scene::PoseLayer::footLock`, `PoseLayerStack::apply`,
`Composition::AnimationSink::driveLayers`
**Tests:** `tests/unit/test_stride_warp.cpp` — "a planted foot slides backwards in the body's
frame", "the lock eases in and out rather than switching", "the lock's drift under acceleration is
0.5*a*t^2, measured"

---

## Context

Phase B §14 asks that a planted foot stay approximately fixed while the body moves, and §15 asks
that it release gracefully rather than teleport. The obvious implementation remembers where the
foot landed and holds it there.

**That implementation cannot survive a scrub, and the reason is structural rather than a bug.**
`EntityWorld::seek` reproduces a frame by replaying the simulation at a fixed 1/60 step — up to
ninety seconds, ADR-273 — and then **poses the rigs once**, at the target time. A remembered anchor
lives in the posing, which runs once; it would therefore hold a position captured from whatever the
rig was doing before the scrub, on a timeline the scrub has abolished.

This is the same family as the three publish-path defects ADR-554 records: **state that only one of
two paths maintains.** Those were found after they shipped. This one is avoided by construction.

## Decision

**The anchor is derived from values already on the seam, and nothing is remembered.**

In the body's own frame a foot standing still in the world slides **backwards at the body's speed**.
So the held target is

```
target -= velocity * elapsedInContact
```

where `velocity` is the measured velocity the seam already carries (ADR-545) and `elapsedInContact`
comes from the contact track the clip already carries (Phase A, ADR-546). Neither is new state and
neither can disagree with a replay, because both are recomputed from the same simulation a seek
reproduces.

Only the **horizontal** component is held. The vertical one is the ground's answer, and holding it
would lift a foot off a slope the character is walking down.

§15's release is an ease at **both** edges over `lockBlendSeconds`, measured against the *time to
the edge* rather than the span's length, so a long stance and a short one release identically. At
both edges the hold is exactly zero, so there is no step for the foot to teleport across.

## What the derivation costs, measured

The derivation assumes the body's speed across the stance is the speed it has now. Under
acceleration that is wrong by exactly `0.5·a·t²`. Measured by integrating a body from rest across a
0.3 s stance:

| acceleration | worst drift | `0.5·a·t²` |
|---|---|---|
| 1 m/s² | **0.0475 m** | 0.045 |
| 2 m/s² | **0.095 m** | 0.090 |
| 4 m/s² | **0.190 m** | 0.180 |

**Does that read on screen?** At Glowmere's scale the alien rig is 1.662 m drawn at ~1.94×, so a
body is about 3.2 m tall and stands ~130 px tall in the `rook` close-up.

* At the cast's **actual** accelerations — they travel at 0.05–0.13 m/s, so most frames are near
  zero and a brisk change is ~0.5 m/s² — the drift is about **0.04 m, or 1.6 px. Invisible.**
* At the **authored peak** (`rook` has `accel: 4.815`) during a start, it would be ~0.2 m, about
  **9 px**, which is marginally visible.

So the honest summary is that a number which sounds large in metres is a pixel or two on this
content, and only approaches visibility during the hardest possible start — which is also when the
stance is shortest and the ease is doing most of its work. `footLock` is a weight rather than a
switch precisely so a scene that hits the bad case can back it off.

## Consequences

* A foot lock works identically in a play and a scrub, by construction, and no test needs to assert
  that separately because there is no state to diverge.
* `footLock` defaults to **0**, which is exactly the behaviour every scene had before this: the
  foot plants on the ground under it and travels with the body.
* **Revisit if** a character appears whose accelerations are sustained and large — a vehicle, a
  thrown body. The fix is then to carry the stance's *start* velocity across the seam, which is one
  more scalar and still not an anchor.
