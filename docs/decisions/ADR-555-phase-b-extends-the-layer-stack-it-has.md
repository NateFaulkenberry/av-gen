# ADR-555: Phase B extends the layer stack it has, and puts the deciders somewhere else

**Status:** Accepted
**Date:** 2026-09-20
**Related:** ADR-300 (a pose layer cannot reach the world), ADR-359 (foot IK on a named chain),
ADR-543 (a leg is three joints and a write-back rule), ADR-551 (`IGroundQuery`), Phase B §1.2
(no monolithic character class), §3 (the procedural motion layer), §63 (keep it reversible)
**Implemented by:** `src/scene/motion_context.hpp`; `scene::PoseLayerKind` as the extension point
**Tests:** `tests/unit/test_motion_context.cpp`, `tests/unit/test_alien_foot_lab.cpp`

---

## Context

Phase B §3 sketches a virtual interface:

```cpp
class IMotionLayer {
    virtual void apply(Pose& pose, const MotionContext& context, float dt) = 0;
};
```

and lists nine candidate layers. It also says the interface should follow AV Gen conventions, and
§2 says to adapt to the actual Phase A implementation rather than recreate the conceptual one.

AV Gen already has a layer system. `scene::PoseLayerStack` holds `PoseLayer` **structs** — name,
kind, drive, joint mask, weight — resolved and applied by the stack, serialized in every scene
file, with per-layer `LayerResolution` diagnostics and `PoseLayerStats`. It is data-driven and
non-virtual on purpose: a layer is something an author writes in JSON, not something a programmer
subclasses.

## Decision

**Phase B adds `PoseLayerKind` values. It does not add a second layer system.**

* A virtual hierarchy would not be scene-serializable, and every Phase B layer must be authorable
  in a scene file — §50 wants a debug view of them and §63 wants each one switchable off, which is
  a weight on an authored layer and nothing more.
* §1.2 forbids a monolithic `AutonomousCharacter`. **Two parallel layer systems is the same failure
  rotated ninety degrees**: the question "which system owns look-at" would then have two answers,
  which is how ADR-260 started.
* The existing stack already carries masking, weighting, resolution reporting and stats. A new
  interface would reimplement all four, differently.

### And the split §3's list hides

The nine layers §3 names are not one kind of thing. They divide cleanly:

| | what it does | where it lives |
|---|---|---|
| Foot placement, terrain adaptation, balance, look-at, aim, reach, secondary motion | **edit a posed skeleton** | `PoseLayerKind`, applied by `PoseLayerStack` |
| Locomotion, speed adaptation, stride warping, turn, start/stop | **choose a clip, a rate and a stride** | the motion controller, above the pose |

Forcing both behind one `apply(Pose&, ...)` would make the locomotion decider pretend to edit a
pose when what it actually does is pick what plays. That is §67's own diagram — `MOTION CONTROLLER
→ BASE POSE → BODY ADAPTATION → IK → SECONDARY` — and the controller is upstream of the pose in it.

### `MotionContext` is the keyhole, not a pointer to the world

`pose_layers.hpp` includes nothing from `entity/` and says so in its first comment. That is what
makes a layer a pure function of a pose and a context, so a scrub replays it exactly and a test can
construct one with no world. `MotionContext` is therefore **scene-tier and entity-free**: the seam
fills it at `AnimationSink::driveLayers`, the one function that already depends on both tiers.

`LocomotionMode` is a deliberate five-value projection of the nine-value `entity::Activity`.
Observe, React, Jump, Fall and Land all matter to a gait machine and none of them matters to a
layer that bends a knee; importing the full enum would drag `entity/locomotion.hpp` into every pose
layer and end the paragraph above in one commit.

Everything spatial in the context is **entity-local**, converted once per frame rather than once
per layer — a posed rig has no world position (ADR-274), and the conversion includes the
inverse-transpose for normals that ADR-359 already paid for.

## Consequences

* Phase B's layers arrive as `PoseLayerKind` values with scene syntax, and each is disableable by
  weight, which is §63 for free.
* The controller is a separate object above the pose, and `IMotionProvider` plugs in there rather
  than into the layer stack — which is what keeps Phase C's motion matcher and Phase E's neural
  provider from having to pretend to be pose layers.
* **Revisit if** a layer needs per-layer state across frames that a scene file cannot express. The
  answer then is probably that the state belongs in the entity tier, where a seek replays it
  (ADR-359 §4), rather than that the layer system was wrong.
