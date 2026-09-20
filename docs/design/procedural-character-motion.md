# Procedural character motion — Phase B

**Status:** in progress. This document is written as Phase B proceeds and is the milestone report
§65 asks for. Phase A's report is `autonomous-character-animation.md`; this one does not repeat it.

---

## B.A — Audit of Phase A

§2 asks for an implementation map before any code: existing primitive → what Phase B reuses → what
Phase B must add. Read off the code rather than off Phase A's own report.

### Pose space — `scene::PoseLayerStack`

| primitive | file | Phase B use |
|---|---|---|
| `PoseLayer` (name, kind, drive, mask, weight) | `scene/pose_layers.hpp` | the layer system. **Reuse, extend.** |
| `PoseLayerKind::{Aim, Additive, Foot}` | same | Phase B adds kinds here, not a parallel hierarchy — see ADR-560 |
| `PoseLayerDrive::{Manual, Look, Reaction, Ground}` | same | how a layer is fed from the entity seam |
| `JointMaskSpec`, `LayerResolution`, `PoseLayerStats` | same | §30 masking and §64 diagnostics already exist |
| `solveTwoBone`, `IkStatus` | `scene/ik.hpp` | §13 foot placement, §23 reach |
| `solveBodyCompensation`, `BodyCompensationLimits/Status` | same | §18 balance, §25 compensation framework |
| `IGroundQuery`, `PlaneGroundQuery`, `TerrainGroundQuery` | `scene/ground_query.hpp`, `composition.cpp` | §17 — **already built as the abstraction** (ADR-551) |

### Clip space — `scene::AnimationPlayer` and friends

| primitive | Phase B use |
|---|---|
| `AnimationPlayer::PhaseMatch`, `transitionOffset`, `inertializeHalflife` | §8 start, §9 stop, §12 mode changes |
| `ContactTrack`, `PhaseTrack`, `ClipAnalysis` | §14 planting, §15 release, §41 metrics |
| `MotionPack` | §42 offline augmentation writes into this |
| `retargetClip`, `RetargetProfile` | **blocked for corpus content** — ADR-553 |

### Entity space — the seam

| primitive | file | Phase B use |
|---|---|---|
| `LocomotionState` (activity, playbackRate, blend, ground, look, reaction, velocity, facing) | `entity/locomotion.hpp` | the behaviour→animation seam; `MotionContext` reads it |
| `EntityState::velocity`, `facing()`, `groundSpeed()`, `strafeAngle()` | `entity/behavior.hpp` | §11 strafe, §35 velocity tracking — **already measured once** (ADR-545) |
| `Gait::approach(current, desired, accel, decel, dt)` | `entity/gait.hpp` | **§34 acceleration/deceleration already exists** |
| `Gait::select` with enter/exit hysteresis + `minDwell` | same | **§12 locomotion modes already exist**, flicker-free |
| `Gait::playbackRate` | same | §6 playback-rate arm, clamped |
| `Gait::footSlip` | same | **§41's stride quality metric already exists and already reports failure** |

**Phase B is not starting from zero on locomotion.** Acceleration limits, gait hysteresis, dwell,
playback-rate matching and a stride-quality metric are all shipping. What is missing is everything
spatial: stride warping, foot planting, terrain adaptation, balance, look, reach.

### The measurement that reframes §6

`Gait::footSlip` is the zero-variance probe for stride quality, it already exists, and it is
already failing across the whole cast. Harvested from one full CPU suite run — **100 distinct
warnings across 51 entities**:

| | |
|---|---|
| slip **< 1** (body slower than its own stride) | **97 of 100** |
| slip **> 1** (body faster than its stride) | 3 |
| median raw `speed / authored` | **0.250** |
| minimum raw ratio | **0.0163** |

The Glowmere aliens are the extreme, and they are the characters Phase B is for:

| entity | travelling | walk clip authored for | raw ratio | slip after rate matching |
|---|---|---|---|---|
| `tide` | 0.05 m/s | 3.07 m/s | **0.016×** | 0.20 |
| `sage` | 0.10 m/s | 3.07 m/s | **0.033×** | 0.40 |
| `vane` | 0.13 m/s | 3.07 m/s | **0.042×** | 0.50 |

Their `rateMin` is **0.08** — already pushed 7.5× below the 0.6 default by someone trying to fix
this — and it is **still saturated**. That is a knob at the end of its travel.

**§6 frames speed adaptation as interpolating between clips** (walk 1.0, run 3.0, desired 1.7).
Measured, that is 3% of the problem. **97% of it is characters moving far below their slowest
authored clip**, typically by 4× and at worst by 61×, where there is no clip to interpolate toward
and playback rate is the only tool available.

This does not contradict the architecture — it reorders it. **§7 stride warping is the load-bearing
piece of speed adaptation on this content, not §6 rate blending**, because shortening a stride in
space is the only thing that helps a body moving at a twenty-fourth of its authored speed. B.E is
scheduled accordingly.

(Rate matching is *off* for 94 of the 100 warnings, so most of the cast is not even using the one
tool that exists. That is a content question as much as an engine one, and it is noted rather than
fixed here.)
