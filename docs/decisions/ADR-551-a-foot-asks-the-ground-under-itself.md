# ADR-551: A foot asks the ground under itself, through an abstraction that hides where the answer came from

**Status:** Accepted
**Date:** 2026-09-20
**Related:** ADR-274 (a joint transform is entity-local; attachments read the previous frame's pose),
ADR-300 (a pose layer cannot reach the world), ADR-359 (the ground is a plane, and the smoothing
lives in the entity), ADR-544 (body compensation), ADR-548, Phase B §17 (terrain query abstraction)
**Implemented by:** `src/scene/ground_query.{hpp,cpp}`, `Composition::groundQuery()`,
`Composition::AnimationSink::driveLayers`
**Tests:** `tests/unit/test_alien_foot_lab.cpp`

---

## Context

ADR-359 gave a foot layer a ground **plane**, and ADR-544 gave the body a way to move when a foot
could not reach. Standing three Glowmere aliens on real terrain showed that the second could never
happen:

* `entity::LocomotionState` carries **one** `groundPoint`/`groundNormal` for the whole body;
* `GroundFollower` seats the body on that plane;
* `plantOnPlane` drops each foot onto **the plane its own body is standing on**.

The target is therefore reachable by construction, however rough the ground underneath is.
Measured: aliens on ground with **0.3499 m of relief across their own 0.9 m footprint** — found
with a probe rather than guessed — and not one foot clamped or compensated. The feet were being
planted on an idealised surface, not on terrain.

## Decision

**Each foot asks the ground under its own posed tip, through `IGroundQuery`.**

```cpp
struct GroundSample { glm::vec3 point, normal; float distance; GroundCategory category; bool valid; };
class IGroundQuery { virtual GroundSample sampleAt(const glm::vec3& worldPoint) const = 0; };
```

**The abstraction is the point, and it was not the first thing built.** The narrow version —
widening the seam to carry four ground points — was written first and then replaced, because it
would have built Phase B §17's seam without its shape and B's foot-placement work would have had to
inherit or replace it. A procedural layer must not know whether the answer came from generated
terrain, a raycast, a collision system or a baked field. `Composition` supplies a terrain-backed
implementation by default and `setGroundQuery` replaces it; `PlaneGroundQuery` is the fallback and
what a test uses when the question is about the layer rather than about the world.

The sample is taken from the **previous frame's pose**, because `EntityWorld::update` runs a whole
stage before the rigs are posed (ADR-274 §5). That is the same frame of lag an attachment carries
and for the same reason: a foot moves a centimetre or two in 16.7 ms and the ground under it does
not move at all, so the staleness costs horizontal sampling position rather than foot height.

## Two bugs this found, both of which had been shipping

**1. `TerrainReject` answers a different question.** Its values — `TooSteep`, `Submerged`,
`NoHeadroom`, `InsideHero`, `Obstructed` — are about whether a character may **walk** somewhere.
Every one of those places still has a surface with a height. Treating a reject as "no ground" made
a foot fall back to the body's plane *precisely where the terrain was steep enough to be worth
sampling*. Only `OutOfBounds` is genuinely no answer.

**2. An off-by-one that read past the end of a vector.** The per-layer loop indexed
`chains()[layerIndex]` where `layerIndex` had already been post-incremented — so the left foot
received the **right foot's chain**, and the right foot read one element past the end of a
two-element vector. It produced plausible output: one foot got a real terrain sample at the wrong
position and the other silently fell back. It was found by asserting that the two feet receive
*different* ground planes and then, when they did, checking whether each plane was the one the
query returned for that foot.

## Evidence

| | |
|---|---|
| the two feet receive different planes | **0.543 apart** in the rig's own units |
| each plane is the terrain under that foot | tip world positions 1.05 m apart, ground heights **40.2138** and **40.1583** — 5.5 cm of real relief |
| both samples valid | `category = terrain` |
| both feet | `Applied`, not `Clamped` |
| body compensation | **not needed on this terrain**, and that is now a correct answer rather than a structural impossibility |

## The explanation changed twice, and only measurement told them apart

Worth recording because the first two were plausible and cheap:

1. *"The fixture is too flat."* Refuted by putting the aliens on measured 0.3499 m relief.
2. *"One plane per body makes every target reachable by construction."* True, and fixed here.
3. *"Both feet reach anyway."* The current answer: `plantOnPlane` preserves each foot's rest height,
   the body is seated at the footprint mean, and the targets land inside what the knee's bend gives.
   ADR-544's 0.0098 m of slack is the **rest-pose** figure; a bent knee has more.

## Consequences

* Foot placement is on terrain rather than on an idealised plane, which is the precondition for
  Phase B §13's foot placement and §16's terrain adaptation.
* Phase B §17's abstraction exists, with per-foot sampling as its first consumer rather than as a
  one-off.
* Cost: one ground query per Ground-driven foot layer per character per frame, plus one
  `poseToModel` per such layer. At 21 characters × 2 feet that is 42 queries; `WorldMap::height` is
  measured at 1.14 µs (`nav_grid.hpp:225`). **Not yet profiled in situ** — the `poseToModel` per
  layer is the part to watch, and hoisting it out of the loop is the obvious fix if it shows.

## Revisit triggers

* A wall-walker or a character in a beam: `sampleAt` answers along world −Y and a direction
  argument would be the change.
* `distance` and `category` are carried and unused. The first consumer that needs "is this water"
  or "how far is the drop" will say whether they are the right two fields.
* The per-layer `poseToModel`, once profiled.
