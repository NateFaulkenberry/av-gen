# Procedural character motion — Phase B

**Status:** in progress. This document is written as Phase B proceeds and is the milestone report
§65 asks for. Phase A's report is `autonomous-character-animation.md`; this one does not repeat it.

---

## Phase B task log

| stage (§61) | state | note |
|---|---|---|
| A. Phase A audit | **done** | the implementation map below; found the stride problem is 97% one-sided |
| B. MotionContext | **done** | ADR-561; found three seam publication defects on the way (ADR-560) |
| C. MotionController — provider seam | **done** | ADR-541 built as written; chain + clip provider held to parity |
| D. acceleration / deceleration | **done** | the vector layer `Gait::approach` does not have |
| E. locomotion adaptation | next | **stride warping first, not rate blending** — accepted reordering, see below |
| F. turn / directional movement | pending | |
| G. foot placement | pending | the first visually checkable milestone |
| H. terrain adaptation | pending | |
| I. body compensation / balance | pending | validated at unit level, never seen to engage in a scene |
| J-L. look-at, reach, secondary motion | pending | |
| M. offline procedural augmentation | pending | |
| N-O. validation metrics, profiling | pending | |
| P. Glowmere vertical slice | pending | |
| Q. documentation / ADRs | continuous | this file |

### An accepted departure from the spec's ordering

Phase B §6 treats speed adaptation as interpolating between clips and §7 treats stride warping as a
refinement. **Measured, that ordering is inverted, and this is recorded as a decision rather than
left to look like drift.** 97 of 100 foot-slip warnings across the shipping cast are the body
moving *slower* than its own stride, median ratio 0.250, with the Glowmere aliens at 0.016–0.042×
and `rateMin` already 7.5× below default and still saturating. There is no clip to interpolate
*toward* in those cases and playback rate is exhausted.

So for this phase: **§7 stride warping is the load-bearing piece and §6 clip interpolation is the
3% case.** B.E is scheduled accordingly. The evidence is in B.A below.

**Blocked, and recorded so the dependency is visible:** ADR-553 (positional retargeting) gates
**Phase C**, not Phase B. Phase B runs on the alien's own 57 seconds of authored clips, whose legs
articulate (reach 0.69–0.96) where the retargeted corpus does not (0.970 flat).

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

---

## B.B — MotionContext

`src/scene/motion_context.hpp`. ADR-561 records why it is scene-tier and entity-free, and why
`LocomotionMode` is a five-value projection of the nine-value `entity::Activity` rather than that
enum. Built once per frame at `Composition::AnimationSink::driveLayers`, the one function that
already depends on both tiers; readable through `Composition::motionContext(node)` for §50's debug
view and for tests.

**What it carries that nothing carried before:** `dt`; the body's own `restHeight`, so a layer's
thresholds are ratios rather than metres (ADR-552's rule, on this side of the pipeline); and
`strideRatio` from `Gait::footSlip`, which is the number Phase B exists to drive to 1.

### The three seam defects it found

Needing to read `LocomotionState` meant asking, for each field, *which of the two publish paths
writes it*. Two of three answers were wrong (ADR-560):

| field | `update` | `seek` | symptom |
|---|---|---|---|
| `velocity`, `facing` | **no** | yes | measured **2.12, 2.12**; seam carried **0, 0** for all of live play |
| `action` | yes | **no** | play published `"Sit"`, scrub published `""` — a sitting body stood up and walked when scrubbed to |
| `grounded` | **no** | **no** | dead |

All three survived because every test asserted on `Entity::state()`, which *computes* the value,
and none on `Entity::locomotion()`, which *carries* it. That is the zero-variance probe rule stated
in the Phase A report, and this is the first thing it caught in Phase B.

---

## B.C — the provider seam

`entity/motion_provider.hpp`, `entity/motion_chain.hpp`, `entity/clip_motion_provider.{hpp,cpp}`.
ADR-541's design, implemented as written: **a provider is a pure function whose memory is owned by
the entity and handed to it.**

* `MotionRequest` is **locomotion intent only**. Look and reach targets are pose concerns that
  already cross the seam as `LocomotionState::lookTarget`; a look does not change which clip plays,
  and duplicating them here would be two answers to one question.
* `MotionMemory` is a plain value with no containers. `EntityWorld::seek` rebuilds it by replay,
  which is why no provider may own it.
* `MotionChain` is **Neural → Motion Matching → Procedural/Clip**, first to produce a pose wins.
  It reports `provider`, `fellThrough` and `firstDeclined` — a chain that silently fell back would
  hide a broken preferred provider behind a working fallback forever.
* `ClipMotionProvider` is a *second, smaller* implementation of clip playback rather than a wrapper
  around `AnimationPlayer`, because the player keeps its state inside itself and ADR-541's whole
  point is that a provider keeps none.

**The obligation, measured.** ADR-541 corollary 1 requires the clip provider to agree with
`AnimationPlayer` for the case they both cover. Asserted joint by joint over 90 frames of a looping
clip at a steady rate, tolerance 1e-4.

### Two findings from the tests

* **The provider dropped the first frame's `dt`** — off by exactly one frame for all ninety
  comparisons. The fixture was the thing at fault: it started the player at t=0 and first asked the
  provider at t=1/60, so the provider selected its clip on that call and sat at local 0 while the
  player was at 1/60. The engine calls every frame including the first, so priming is what actually
  happens. ADR-204's one-frame family, fourth occurrence this session.
* **An empty entry style was a wildcard**, so a request for a style nothing carries resolved
  silently to the default walk. A style typo, or a pack that shipped without its styled clips,
  would have been invisible. Now matched exactly; a catch-all is authored explicitly (§64).

---

## B.D — acceleration, deceleration and the vector layer

`entity/motion_controller.{hpp,cpp}`. §34 and §35.

**What already existed.** `Gait::approach(current, desired, accel, decel, dt)` limits how fast a
*scalar* speed may change, with separate limits up and down, and `Gait::select` owns mode selection
with hysteresis and a dwell timer. B.D does not duplicate either.

**What was missing is the vector layer.** A scalar speed along a heading describes a body that
walks where it looks and nothing else. A body that strafes, backs up or circles a target has a
velocity and a facing that disagree, and "how fast may that change" is then two questions:

| | limited by | why |
|---|---|---|
| along the heading | `maxAcceleration` / `maxDeceleration` | stopping is not the reverse of starting |
| across it | `maxTurnRate` (rad/s) | a body at 5 m/s and one at 0.5 m/s take about the same time to come round |
| the body's facing | `maxFacingRate` (rad/s) | turning the shoulders costs less than turning the momentum |

**The obvious implementation is wrong and the test says so.** Lerping the velocity vector toward
the desired one at `maxAcceleration` turns a slow body almost instantly and a fast body barely at
all — backwards. The discriminating case is two bodies, at 1 m/s and 8 m/s, both asked to turn 90°:
they must turn by the same angle in one step, and they do (`maxTurnRate * dt`, to 1e-4).

Three more properties the tests pin: a standing body may set off in any direction without turning
first (`headingFloor` — otherwise a body asked to walk backwards from rest drifts visibly for a
quarter second first); steering is **added**, not blended (§39), so a correction that exactly
cancels the request stops the body; and `dt <= 0` changes nothing and divides by nothing (ADR-521).

Like `MotionMemory`, `MotionState` is a plain value the entity owns, so a seek replays it.
`MotionSolution` reports `accelerationLimited` and `turnLimited` rather than clamping silently —
B.A found exactly that failure in `Gait::playbackRate`, where the shipping cast has been sitting on
the clamp floor with nothing saying so.

---

## The seam, closed

`MotionContext` was wired from the start. `MotionChain`, `ClipMotionProvider` and the controller
were tested and **called by nothing**, which is this repository's most expensive recurring failure.
They are reachable now.

### The split the wiring forced

ADR-541 specified one `evaluate` that advanced the memory and posed the skeleton together. That
cannot work here, for a measured reason: `EntityWorld::seek` reproduces a scrubbed frame by
replaying the simulation at a fixed 1/60 step for up to **ninety seconds** — 5,400 steps per body —
and then poses the rigs **once**. A seam that posed an 89-joint skeleton on every replay step would
multiply the worst interactive cost in the product by 5,400.

So `IMotionProvider` has two methods:

| | runs | may touch | why |
|---|---|---|---|
| `advance` | every step, including every replay step | no skeleton | it is what makes a scrubbed frame reproduce a played one |
| `pose` | once per drawn frame | the skeleton | a pure function of memory `advance` already settled |

This is not a compromise. It is the shape these algorithms already have — learned motion matching is
a Stepper that advances a latent and a Decompressor that turns it into a pose; classical matching is
a search then a lookup. ADR-541's single call was hiding that seam, not simplifying it.

`MotionMemory` records **which provider** settled it, so `pose` returns to the one that won
`advance`. A chain that re-selected at pose time could hand a matcher's database frame to the clip
player, which would read it as a clip index and draw a different animation entirely.

### Where each piece lives

- **`Entity` owns the memory** (ADR-541) and `Entity::advanceMotion` steps it. Called from **both**
  publish paths — ADR-560's rule, applied to the very struct that rule was discovered by.
- **`SkinnedRig::externalPose`** is a pose, not a provider. The scene tier deliberately does not
  learn what put it there; if it did, the layer module would have a route to the simulation, which
  ADR-300 exists to prevent. It is **consumed** rather than latched, so a driver that stops driving
  hands the body back to its clips instead of freezing it.
- **Everything after the base pose is unchanged**: root motion, the layer stack, foot IK and the
  palette all run exactly as they do over a clip. That is the point — foot IK does not care where
  its base pose came from.
- **The opt-in is `EntityDesc::proceduralMotion`**, absent in every scene that exists. It is written
  back only when true, so saving an untouched project produces the bytes it had.

### Condition 1 — default-off is provably inert

60 frames of `glowmere-valley-2-multicam`, rendered **through the project** (ADR-264: a project's
parameters are applied over its scene, so a measurement from the scene file measures a file nobody
renders). Sequence hash = sha256 of the per-frame sha256 list.

| arm | binary | opt-in | sequence hash |
|---|---|---|---|
| before | `main` @ `c14a7644` | — | `94a86f3db7a6198c…` |
| after | this branch | off (default) | `94a86f3db7a6198c…` |
| **control** | this branch | **on**, 5 aliens | `f4a477b7c5efe1c4…` |

**Byte-identical before and after. Different when the opt-in is on**, so the comparison is not
vacuous (ADR-182). Reproducibility established first: the same build rendered twice gives the same
hash, or none of the above would mean anything.

**A near-miss worth recording.** The first "before" render came from a second worktree and produced
`087bba328f8f…` — a different hash, which read as *my change is not inert*. It was the second
worktree's own asset copies. Running **`main`'s binary against this worktree's files** gave
`94a86f3db7a6198c…`, isolating the binary as the only variable. The lesson is the one ADR-170 already
teaches about the GPU: change one thing, not two.

### Condition 2 — the probes fail when they should

Two probes, each shown red against a deliberate break.

**Probe 1 — the memory advances.** Break: delete the `advanceMotion` call from the live path (the
wiring that genuinely was missing before this stage).
```
CHECK( memory.generation > 0 )   ->  0 > 0
CHECK( memory.provider == 0 )    -> -1 == 0
CHECK( memory.hasPhase )         -> false
CHECK( memory.localTime > 0.0f ) -> 0.0f > 0.0f
```
That is exactly what "built but unreachable" looks like from the outside.

**Probe 2 — the pose reaches the drawn rig. The first version of this probe was vacuous, and only
the deliberate break revealed it.** Break: compute the pose and never hand it over. The probe
asserted `posedByProvider`, which is *the sink's own claim*, and passed. Comparing the drawn pose
could not have caught it either: a clip provider and the clip player agree by design (ADR-541
corollary 1), so the wrong answer and the right one are the same picture.

The fix is evidence from the **consumer**: `SkinnedRig::externalPoseFrames`, counted by `evaluate`
each time it actually uses an external pose. Against the same break:
```
CHECK( debug.externalPoseFrames > 50 )
  with expansion:  0 > 50
  with message:    posedByProvider 1 externalPoseFrames 0 status produced
```
A flag set by whoever *claims* to have done the work cannot tell that apart. A counter incremented
by whoever *consumed* it can.

### And it fires on the real cast

```
entity 'rook': procedural motion on, 3 clip entry(s), chain of 1
entity 'tide': procedural motion on, 3 clip entry(s), chain of 1
entity 'sage': ...   'ember': ...   'vane': ...
```
A chain that resolves **zero** entries warns rather than falling silently back, because a silent
fallback is the failure this whole stage was closing.
