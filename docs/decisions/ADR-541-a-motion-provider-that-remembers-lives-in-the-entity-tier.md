# ADR-541: A motion provider that remembers lives in the entity tier, not in `scene/`

**Status:** Proposed (Phase 0 research; not to be implemented before review)
**Date:** 2026-09-20
**Related:** ADR-086 (the player stores *when*), ADR-088 (the behaviour/animation seam), ADR-091
(two-tier determinism), ADR-260 (three positions), ADR-274 (a joint transform is entity-local),
ADR-300 (the layer stack is `PoseOnly` by construction), ADR-337 (root motion is the one exception),
ADR-359 (the solver is a pure function of the pose), ADR-360 (a render must be reproducible)
**Full analysis:** `docs/design/autonomous-character-animation.md` §9

---

## Context

Everything in `src/scene/`'s animation path is a pure function today, and each unit says so and
gives its reason:

* `AnimationPlayer` stores *when* a state was entered, never how long it has run
  (`src/scene/animation.hpp:8-16`).
* `PoseLayerStack::apply` integrates nothing and remembers nothing across frames, explicitly
  because "ADR-091 asks the baked tier for scrub == play, and a layer that carried state would be
  the thing that broke it" (`src/scene/pose_layers.hpp:21-27`).
* `solveTwoBone` is analytic rather than iterative because "an iterative solver seeded from the
  previous frame's answer is state carried across frames" (`src/scene/ik.hpp:6-13`).
* `SkinnedRig::rootMotionAt` is a pure function of (player, clips, now) and deliberately *not* a
  read of the pose (`src/scene/animation.hpp:295-305`).
* Smoothing that a look-at or a foot plant needs lives one level up, in the behaviour, "and that
  layer is re-simulated on a seek" (`src/scene/pose_layers.hpp:27-28`, `src/entity/grounding.hpp:88-91`).

Motion matching does not fit that pattern. It is inherently stateful: the controller plays a
database frame, advances it, and searches only every N frames, jumping only when a candidate beats
the current frame by a margin. The frame it is on is memory. Inertialization is memory too — it is
a pose offset being decayed from a known start.

If that memory is put on `SkinnedRig`, the first scrub breaks it, and it breaks it in the way this
project has paid for repeatedly: silently, as a small positional disagreement nobody can attribute.

## Decision

**`IMotionProvider` is a pure function whose memory is owned by the entity and handed to it.**

```cpp
[[nodiscard]] virtual bool evaluate(const MotionRequest& request, const MotionMemory& in,
                                    const scene::Skeleton& skeleton, scene::Pose& out,
                                    MotionMemory& next) = 0;
```

`MotionMemory` — the database frame, the local clip time, the generation counter, the latent state,
the inertialization start — is a plain value held by `Entity`, reset by `Entity::reset` with
everything else, and reconstructed by `EntityWorld::seek`'s fixed-step replay. The provider reads
it, writes the next one, and owns nothing.

This is the same split ADR-359 drew for the ground plane and ADR-337 drew for root motion, applied
one layer out. Obligation **D4** in `src/entity/character_ai.hpp:76-94` already states it as a
rule: *memory is bounded and reconstructed by replay, never persisted.*

Three corollaries:

1. **`IMotionProvider` is a sibling of `AnimationPlayer`, never a replacement.** Every character can
   run in Clip mode; Clip mode is the fallback for every failure. The first implementation must
   produce output identical to today's.
2. **No provider names a clip to a behaviour.** Rule R4 (`src/entity/character_ai.hpp:57-60`) is
   unchanged: a `MotionRequest` names an intent, and the motion pack maps intent to content.
3. **A provider that returns false must say why, and the reason must be visible.** The same rule
   `LayerResolution`, `IkStatus`, `PathStatus`, `SocketResolution` and `ActionResult` already
   follow.

## The representation change this forces

`EntityState` describes velocity in polar form — `{float speed; float yaw;}`
(`src/entity/behavior.hpp:50-91`) — and every behaviour integrates `travel` itself. That cannot
express "moving north-east while facing north", which is the first thing a trajectory feature, a
directional blend space or a strafe needs. **`EntityState` gains a velocity vector.** It is a small
change with a wide blast radius, and it is a prerequisite rather than a nice-to-have.

## Rejected alternatives

* **Put the frame index on `SkinnedRig`.** A rig is shared across entities
  (`src/entity/locomotion.hpp:134-140`: "a posed rig cannot have a world position, because it stands
  in as many places as there are bodies carrying it"). Two aliens matching different motion would
  fight over one index.
* **Make the provider own its own state and re-seed it on seek.** That is the interface this
  project has repeatedly found does not survive: `reseedAfterDiscontinuity` exists on `SkinnedRig`
  precisely because "at seek time the rig has not been re-posed yet"
  (`src/scene/animation.hpp:270-278`). Adding a second such flag doubles the surface.
* **Accept scrub/play divergence.** ADR-360 permits scrub to differ from play, but it does *not*
  permit two renders of the same range to differ, and provider state on a shared rig would break
  the second guarantee too.
* **A `MotionRequest` that is just `LocomotionState`.** `LocomotionState` is the right seam and the
  wrong vocabulary: it has no velocity vector, no trajectory, one ground plane for the whole body,
  and no way to say "urgently". It should be extended or superseded, deliberately, rather than
  quietly overloaded.

## Consequences

* A stateful motion system becomes testable with the harness that already exists —
  `tools/charai_probe.cpp`'s play-vs-seek comparison, which measured 0.000022 m at 30 s.
* The provider interface is usable from a test with no scene, no rig and no composition, in the
  same way `Gait` and `solveTwoBone` are.
* A learned provider inherits this for free: its state is `(x, z)`, two float vectors, and it has
  no search and no data-dependent branching — which makes it *more* reproducible than a search.

## Revisit triggers

* A provider appears whose state cannot be bounded to a fixed-size value type.
* Rig posing moves off the main thread, at which point `MotionMemory`'s ownership must be re-read
  (the entity update is single-threaded by assumption in `nav_grid.hpp:349-351` and
  `perception.hpp:98-101`).
