# Procedural character motion — Phase B

**Status:** in progress. This document is written as Phase B proceeds and is the milestone report
§65 asks for. Phase A's report is `autonomous-character-animation.md`; this one does not repeat it.

---

## Phase B task log

| stage (§61) | state | note |
|---|---|---|
| A. Phase A audit | **done** | the implementation map below; found the stride problem is 97% one-sided |
| B. MotionContext | **done** | ADR-555; found three seam publication defects on the way (ADR-554) |
| C. MotionController — provider seam | **done** | ADR-541 built as written; chain + clip provider held to parity |
| D. acceleration / deceleration | **done** | the vector layer `Gait::approach` does not have |
| E. locomotion adaptation | **done** | `PoseLayerKind::Stride`; accepted reordering, see below |
| F. turn / directional movement | **partly done** | `stepMotion` has the rate limits (B.D); the pose half is lean, below |
| G. foot placement | **already existed** | `PoseLayerKind::Foot`, ADR-359/543 — Phase A built it |
| H. terrain adaptation | **already existed** | `IGroundQuery` per foot, ADR-551 — Phase A built it |
| I. body compensation / balance | **already existed** | `solveBodyCompensation`, ADR-544 — Phase A built it |
| J. look-at | **already existed** | `PoseLayerKind::Aim` + `PoseLayerDrive::Look`, ADR-300 |
| K. reach | **cut, named** | nothing in Glowmere reaches for anything; the machinery it needs (`solveTwoBone`, a chain, a target) is the foot layer's, so it is a small addition when a scenario wants it |
| L. secondary motion | **done** | `PoseLayerKind::Secondary`; a sine of the timeline second, not a noise field |
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
| `PoseLayerKind::{Aim, Additive, Foot}` | same | Phase B adds kinds here, not a parallel hierarchy — see ADR-554 |
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

`src/scene/motion_context.hpp`. ADR-555 records why it is scene-tier and entity-free, and why
`LocomotionMode` is a five-value projection of the nine-value `entity::Activity` rather than that
enum. Built once per frame at `Composition::AnimationSink::driveLayers`, the one function that
already depends on both tiers; readable through `Composition::motionContext(node)` for §50's debug
view and for tests.

**What it carries that nothing carried before:** `dt`; the body's own `restHeight`, so a layer's
thresholds are ratios rather than metres (ADR-552's rule, on this side of the pipeline); and
`strideRatio` from `Gait::footSlip`, which is the number Phase B exists to drive to 1.

### The three seam defects it found

Needing to read `LocomotionState` meant asking, for each field, *which of the two publish paths
writes it*. Two of three answers were wrong (ADR-554):

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
  publish paths — ADR-554's rule, applied to the very struct that rule was discovered by.
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


---

# Phase C — motion library and motion matching

## C.1 The database (§4-§9)

`src/scene/motion_database.{hpp,cpp}`. Built offline from a `MotionPack`, searched at runtime, and
deliberately a different shape from the pack: a pack stores clips so they can be *played*, a
database stores features so they can be *compared*.

**Memory (§6).** No `MotionSample` object — parallel arrays plus one contiguous feature block, so a
query touches memory linearly. Measured at 33 dimensions:

| corpus | clips | samples | features | metadata | total | per sample |
|---|---|---|---|---|---|---|
| Glowmere alien, own clips | 26 | 1,738 | 0.22 MB | 0.03 MB | **0.25 MB** | 152 B |
| 100STYLE, retargeted | 24 | 85,610 | 10.78 MB | 1.63 MB | **12.41 MB** | 152 B |

Build throughput **24,000–36,000 samples/s**. Extrapolated, 100STYLE's full 4.78 M frames is
**~700 MB** and about 2¼ minutes — which is a fact worth knowing before anyone asks for it.

**Normalization (§9)** is zero mean, unit standard deviation per dimension, applied once at build
so a query is a plain squared distance. The convention is stated rather than tuned: a dimension's
own spread *in this database* is what makes metres comparable to metres-per-second, and a
hand-authored scale per unit would be a second set of weights fighting the first.

## C.2 The finding: a zero-variance check is necessary and not sufficient

I added a dead-dimension count expecting it to expose the in-place corpus. **It reported zero dead
dimensions on both corpora**, including the one ADR-553 proved cannot move its own legs.

The reason is worth stating exactly. A foot welded to a pelvis that *rotates* has feature
coordinates that change on every frame — x and z sweep round — while the thing that would reveal
the weld, the **length** of that offset, never changes at all. Variance in the raw dimensions is
therefore the wrong test.

The right one is rotation-invariant: the spread of each feature joint's **distance from the body**.

| corpus | `foot.l` | `foot.r` | `head.x` |
|---|---|---|---|
| alien, own clips | **0.0869** | **0.0980** | 0.0142 |
| 100STYLE, retargeted | **0.0000** | **0.0000** | 0.7067 |

That is ADR-553 confirmed from an unrelated direction and **more precisely than the original
finding**: the retarget drives the upper body correctly — the head moves 0.71 — and the legs not at
all. The database reports this per joint on every build.

## C.3 Search (§10-§17)

Linear scan with an early out, continuity cost (§11) and transition cost (§12) keyed on **tags,
never clip names** (§12 is explicit). Tag filtering (§14) runs before scoring: one AND per sample
against 33 multiply-adds.

Benchmarked on **queries drawn from the database and perturbed** — what a character actually asks —
rather than white noise, for the reason ADR-540 paid to learn: a white-noise fixture made an early
out look 2.23× *slower* when on real near queries it is a 0.80× win.

| samples | ms/query | queries/s | ns per sample scored |
|---|---|---|---|
| 1,738 | **0.0172** | 58,050 | 9.9 |
| 85,610 | **0.5074** | 1,971 | 5.9 |

49× the samples for 29.5× the time: the early out and cache behaviour both improve with size.
A 60-character scene searching ten times a second over the alien's own database costs **10.3 ms/s
of wall clock**, or about 1% of one core.

## C.4 The matcher as a provider

`src/entity/match_motion_provider.{hpp,cpp}`, on ADR-541's chain ahead of the clip player.

**The part that looks impossible.** A motion-matching query needs the character's current pose, and
ADR-556 forbids `advance` from touching a skeleton. It resolves because the current pose **is a
database sample** — the character is playing frame N of clip C, so the pose half of the query is
that sample's own feature vector, already extracted and already normalised. Only intent is
computed. That is not a trick to satisfy the interface; it is how motion matching is formulated.

Search frequency (§27), minimum continuation (§29) and a switch margin (§28), because a matcher
that searched every frame would pass every quality test and cost six times as much. Measured over
one second at 60 Hz: **8–13 searches, 45+ frames continued** by following `sampleNext`.

## Cut, and named

- **§16 two-stage search** and **§15's KD-tree/PCA arms**: not built. The linear scan is 5.9 ns per
  sample and the whole Glowmere database is 0.25 MB — it fits in L2. A tree would be measured
  against a baseline that is already fast enough for the content that exists, which is how
  ADR-540's synthetic-fixture mistake happens. The benchmark harness is in place for the day a
  corpus makes it worth it.
- **§21 motion augmentation** (mirroring, time-warping to synthesise coverage): not built.
- **§23 database quality analyzer**: partly — the per-joint articulation statistic above is the
  part that earned its place; coverage analysis (§22) is not built.


---

# D2 — the Glowmere integration

## What changed in the scene

All five aliens (`rook`, `tide`, `sage`, `ember`, `vane`) in
`glowmere-valley-2-multicam.scene.json`. They already carried a `look` aim layer and a `startle`
additive layer; they now carry **seven**:

| layer | what it does |
|---|---|
| `foot.l` / `foot.r` | plant on the terrain under each foot (ADR-551), on ADR-543's detached chains |
| `stride.l` / `stride.r` | shorten the step to match how fast the body is actually travelling |
| `life` | secondary motion — breathing and weight shift, faded out while walking |
| `look`, `startle` | unchanged |

Plus `contacts`, `matchPhase`, `inertialize: 0.12` and body compensation. Each alien gets slightly
different numbers — stride floor 0.35–0.45, lift 0.70–0.80, breath 1.1–1.7° over a 3.8–5.1 s
period, and five different phases — so they read as individuals rather than as one animation
played five times.

**The edit is surgical**: 400 insertions, 5 deletions, and the only removals are the five array
brackets that now have content after them. Re-serialising the file would have produced a
9,600-line diff against a file the owner may be editing.

## What is visible

Rendered through the project (ADR-264), `--range 4.2:7.8`, which is `rook`'s own camera shot.
Artifacts in `build/review/` (gitignored): `rook-before-after-frame210.png` is the side-by-side.

**Before** (left): the alien is in a wide mid-stride — legs well apart, torso pitched forward, the
leading foot hanging above the plants rather than resting on them. That is the 0.033× stride
mismatch B.A measured, drawn: the body is travelling at 0.10 m/s while playing a walk authored for
3.07 m/s, so it takes a full stride it has no distance to spend.

**After** (right): the stance is narrower, the torso is upright, and both feet are down on the
ground surface. The head is lifted and turned, so the character's emissive eyes and mouth are
toward camera.

**A caveat I am stating rather than glossing.** At frame 210 — 3.5 seconds in — some of that
difference is *simulation divergence*, not a direct layer effect: posing feeds back into the
entity through sockets (ADR-274), so two runs drift apart over seconds. The cleanest read of the
layers alone is the earliest frames, where the difference is 0.13% of pixels confined to a band at
the aliens' feet. The pose change is real in both; the attribution is cleaner early.

## The honest gap: `proceduralMotion` is OFF in Glowmere

The provider chain is opted **in** on the foot lab's `alien-provider` and **out** on all five
Glowmere aliens, and that is a deliberate quality decision rather than an oversight.

`ClipMotionProvider` has parity with `AnimationPlayer` for steady-state playback (asserted joint by
joint over 90 frames) and **does not implement transitions or inertialization** — ADR-547's
machinery belongs to the player and the provider does not yet use it. Glowmere's aliens change gait
constantly, so switching them to the provider tonight would have replaced a blending player with a
non-blending one: an architecture win that reads on screen as a regression.

So the seam is proven reachable where it can be proven (the lab, with two probes each shown failing
against a deliberate break) and left off where it would look worse. **Named as the first thing to
fix before the provider drives production characters.**

---

# Phase B, second pass — the stages the scope cut had removed

Scope-cut authority was revoked for A–D. This section is the running note of each outstanding
stage as it lands.

## B §19 — movement lean (the pose half of B.F)

`PoseLayerKind::Lean`. Tilts the body into what it is doing: pitch from forward acceleration, roll
from lateral acceleration plus turn rate.

**Acceleration is measured once**, at the entity, beside the velocity it differences — ADR-545's
rule one derivative out — and published on **both** seam paths (ADR-554). Lean, stride and balance
all want it, and three subsystems differencing the same vector is how the engine got two answers to
"where is this body" (ADR-260).

Driven by the **measured** acceleration, not the desired one: a body leans into the force it is
actually under, and one leaning into an acceleration its legs were never given is falling over on
purpose.

The clamp is a **magnitude on the pair**, so a diagonal acceleration stays diagonal rather than
squaring off against a per-axis limit. Tested by stating the prediction as a magnitude
(`magnitude == 9.0 ± 0.4`, `|pitch| - |roll| < 0.5`) rather than as "does it clamp" — testing.md
#20.

Measured: 3 m/s² forward gives −6.6° pitch and 0.0° roll; 3 m/s² lateral gives 0.0° pitch and
−6.6° roll. Half weight gives half the angle.

## B §8, §9, §10, §11 — start, stop, turn-in-place, strafe

`entity::planLocomotion`, a pure function with its memory owned by the entity and replayed by
`EntityWorld::seek`.

**Not a second gait machine.** `Gait::select` owns the clip *family* (ADR-096) and has no notion of
the transitional states. This reads the gait's answer and adds a phase to it — a body does not go
from standing to walking, it goes from standing to **starting** to walking — so there is one answer
to "which clip family" and a second, orthogonal one to "where in the arc".

| stage | what it does |
|---|---|
| §8 start | `Starting`, with **two** exits: reaching the asked-for pace, or running out of start. The second is not optional — a body asked for 4 m/s that can only manage 0.3 would otherwise ramp its stride forever |
| §9 stop | `Stopping`, easing the stride out to a **final step, never to zero**. A stride scale of zero is both feet in one place, which is the fade §9 forbids |
| §10 turn | `Turning` for a body turning on the spot, distinct from `Starting` so a stride is not ramped for a body going nowhere |
| §11 strafe | `Strafing` above 35° between heading and facing, back to `Moving` below 22° |

**The braking test is the one that matters for this cast.** A stop triggers when the body is asked
to *shed* speed (`desired < speed × 0.35`), not when it is slow. B.A measured 97 of 100 of the
shipping cast travelling below a quarter of their authored stride — a "is it slow" test would have
put every one of them into a permanent `Stopping`. Both arms are tested: a body that always creeps
stays `Moving`; one that was travelling and is asked to stop goes to `Stopping`.

**Flicker, measured rather than argued.** Three seconds sitting exactly on the strafe threshold,
wobbling ±1° every frame — 180 opportunities to change phase. A single-threshold machine changes
180 times; this one changes **at most 2**. Both mechanisms are needed and both are present: an
enter/exit band and a minimum dwell, which is the pair `GaitSettings` already paid for.

**Wired, not shelved.** The plan runs on both publish paths, the phase and its stride ramp cross
the seam, and the stride layer **multiplies** the phase ramp into the travel ratio — two
independent reasons a step should be shorter, rather than one overwriting the other.

`MotionContext` projects it as `scene::MotionPhase` rather than importing `entity::LocomotionPhase`,
for the reason ADR-555 gives about `LocomotionMode`: naming the entity type in a scene header would
drag `entity/locomotion.hpp` into every pose layer and end the rule that makes a layer a pure
function a scrub can replay.

## §45 — validated on the alien, and the question it was carrying

§45 asks the stack be validated on the real character rather than on fixtures. It arrived carrying
a specific question from §5, which is better than a general one: §5's control had refuted my own
diagnosis of a clamping foot, and the real cause was that on a chain whose steps are not ancestor
links, `solveTwoBone` reads its bone lengths from the pose it is handed. **Is that visible on the
shipping alien, or only on a rig I built with no slack?**

It is visible. Across all 26 clips of `alien-scout.glb`:

- `leg.l upper` **0.0%**, `leg.l lower` **0.0%**, `arm.l lower` **0.0%**, `arm.l upper` **21.4%**
  (worst on `Crazy`, **14.3%** on an ordinary `Walking`).

The three zeros are not "no translation channel" — all 26 clips carry translation on all eight
joints. They are channels whose values never leave the rest translation. **The legs are safe by
accident, not by structure**, and the rig is flat: every step of both chains is a sibling hop under
`rig` or `root.x`, so nothing about the skeleton preserves any of these lengths.

The consequence, measured rather than argued: over `Walking` the arm's `maxReach` runs
**0.5091 m to 0.5398 m**, a 0.0307 m band — 5.7% of the arm. A Reach layer holding a fixed world
point in that band reports `Solved` on some frames and `Clamped` on others; the hand leaves the
target and returns once per stride.

Then the conversion that the foot-lock drift needed too. At Glowmere's cast scale that band is
6.0 cm. The valley cameras sit 170 m out at a 40° vertical field, which at 1080p is 8.7 px/m — the
whole alien is sixteen pixels tall — so the band is **0.52 px**. At a character-scale framing it is
**20.3 px**. Real on shipping content; invisible at the only framing that exists today.

Recorded as ADR-601, with the fix deliberately not made. The obvious fix is optional explicit
lengths on `TwoBoneChain` defaulting to "derive from the pose": structural, no-op for every existing
caller, an afternoon. It is also exactly ADR-600 — a knob nobody sets refuses nothing, and shipping
it would produce an ADR claiming the issue was handled and a test proving the inert default is
unchanged. Two things have to be true first: a Reach consumer at a framing where 20 px matters, and
a decision about `assets/farm`, whose bulls bind at 100% of their own span and are the callers a
rest-derived length would change most.

The leg zeros are asserted, not noted, because the foot lock is the layer that holds a fixed world
point and it runs on the chain that is currently safe for no reason anyone chose. A re-export that
keyframes a hip fails `test_alien_validation.cpp` and says which segment moved.

Also from §45's gate pass over real content: 26 clips measured, 13 fail the §44 limits. That is the
expected shape — `Dying_forward`, `Crazy` and the rest of the non-locomotion half are not clips the
gate's foot-slide and contact-height limits were written for — but it means **the §44 limits are a
locomotion gate, not a clip gate**, and a generator that feeds it non-locomotion source will reject
everything it is given. Noted for §46's slice, which is where a generator first has a source.

## §46 — the first true vertical slice

One continuous 14.5-second run, 870 frames, driven by scripted `MotionRequest`s as §55-57 permits:
idle → accelerate → walk → turn → curve → slope → look → slow → stop → reach → return. Top speed
1.41 m/s, facing swept 270°, ground moved 0.00 → 0.35 m, the look layer resolved on 344 frames and
the reach on 176.

The word doing the work in "should look like a continuous character motion system, not a collection
of disconnected demos" is *continuous*, and it is measurable: no joint may move further in one frame
than a body at this speed accounts for. Three arms, because "nothing jumped" is satisfied by a stack
that does nothing — continuity, then seven separate checks that the run actually happened, then a
control arm that runs the identical script with the motion controller bypassed and must breach the
bound.

**The first run breached it by an order of magnitude, and nobody had authored any of it.** The
causes, each measured after the previous fix: 0.803 m (the reach layer switching on at full weight
in one frame), 0.301 m (`strideRatio` stepping 0.3 → 1.0 at a threshold), 0.231 m (the script's own
terrain teleporting between beats), 0.150 m (the stride weight finishing its sweep in four frames),
0.098 m (a 0.8 m reach blended over 0.25 s — a hand at 3.2 m/s), and then 0.072 m, which is the walk
clip's own loop seam and the floor.

The first was identical with the controller bypassed, which is how it was clear the controller was
not at fault — the same shape as §5's control refuting my diagnosis of the clamping foot, and the
second time in this phase that the arm built to check something else was the arm that found the
answer.

Recorded as ADR-602, whose two rules are:

1. **A layer arrives over a stated duration**, derived from an elapsed time rather than accumulated
   (ADR-557: the pose tier poses once on a scrub) and expressed in *elapsed seconds* rather than an
   absolute one (ADR-086: the stack runs on the rig's clock, the driver knows the entity's — the
   first implementation mixed them and nothing failed, because nothing rate-limits a rig with a look
   layer yet). `blendSeconds = 0` is the old behaviour to the bit, and because a default nobody sets
   is ADR-600, `driveLayers` sets it on the shipping `Look` drive and `LocomotionState` carries the
   schedule, published by one helper that both `update` and `seek` call (ADR-554).
2. **A parameter that snaps at a threshold should have been a weight.** `Gait::footSlip` returns
   1.0 the moment activity leaves Walk — correct about the meaning, wrong about the transition,
   because a neutral value is not neutral when the thing reading it scales by it. The ratio now runs
   continuously to zero and the weight falls off instead; and the weight *is* the clamped ratio,
   one coefficient rather than two.

Three of the six causes were in the driver rather than the stack, and one was in the test's own
script. That last distinction is kept rather than smoothed over: a continuity bound is only
meaningful over inputs a world could actually present, so the fix was the script, not the bound.

## §47 — performance baselines

Measured on the Glowmere alien (90 joints, 26 clips), minima over five repeats, never means.
Microseconds per character per frame, layers switched on cumulatively so each column's difference
from the last is that kind's marginal cost:

| | clip only | +stride (2) | +lean (1) | +feet (2 IK) | +look (1 aim) | +reach (1 IK) |
|---|---|---|---|---|---|---|
| n=1 | 4.74 | 8.87 | 8.94 | 14.37 | 18.88 | 21.43 |
| n=10 | 4.89 | 9.06 | 9.12 | 14.55 | 19.01 | 21.59 |
| n=50 | 4.91 | 9.08 | 9.14 | 14.55 | 19.04 | 21.59 |
| n=100 | 4.92 | 9.07 | 9.26 | 15.03 | 19.20 | 21.91 |

- **Shared cost stays shared**: +0.9% per character from n=1 to n=100. Nothing in the stack
  duplicates the skeleton, the clips or the resolved masks per instance, and the assertion is
  written so that it would if it did.
- **The steady state allocates nothing**: net live heap blocks over a whole repeat is zero at every
  count, read from the default malloc zone rather than by overriding global `operator new` — which
  would have been a change to a binary four agents run.
- **Baseline: 2.16 ms per frame for a hundred characters, full stack.** A measurement on this
  machine today, not a promise.

The number that does not fit the intuition is the aim layer: a look costs 4.68 µs, more than a
two-bone IK solve at 2.75 µs. The reason is that every model-space layer calls `poseToModel` over
all 90 joints to read one to three of them, seven times per character per frame counting the body
compensation pre-pass. One walk measures 2.048 µs, so **the walks are 14.3 µs of the 21.6 µs total —
66% of the stack, against solves that are the small part.**

ADR-603 records that, and records that it **cannot be hoisted**: a layer writes the pose and the
next needs model space as the previous layers left it, which is the entire content of §5's ordering
contract. One shared rebuild at the top would be fast and silently wrong. The correct optimization
is incremental — rebuild only beneath what the previous layer wrote — and the stack already knows
what each layer touches, because `masks_`, `chain_` and `stride_` are resolved at bind time. That is
§53's work, and the expected win is stated in the ADR *before* the work so that it can be wrong.

## §48 — multi-character architecture

§47 proved the per-character *time* does not grow with the count. §48 asks the question timing
cannot answer, and the answer came from reading a header rather than running anything:
`scene::SkinnedRig` holds `Skeleton skeleton` **and `std::vector<AnimationClip> clips` by value.**

On the shipping Glowmere scene, after ticking the composition so its rigs are installed — counting
straight after `loadFile` reports a confident 0.00 MB, which is `docs/testing.md` family C:

- 14 rigs, **15.45 MB** of skeleton and clip data.
- Five 90-joint aliens at **3.01 MB each**, clips **byte-identical** across all five by FNV-1a over
  the key times and values. (The first version of the test compared clip *names and sizes*, which
  two rigs could satisfy while animating differently in every key.)
- **12.12 MB — 78% of the total — is a byte-identical second copy.**

The five aliens are five different GLB files with different meshes shipping the same 26-clip pack,
so the duplication is in the assets as well as in the instancing: one of each character already
carries five copies.

12 MB is not alarming, which is why ADR-604 records the extrapolation instead: **a hundred
characters of this rig is 301 MB of animation data of which 298 MB is the same bytes.** The fix
belongs at load — `AssetRegistry` already keys by source path, every consumer already takes
`const std::vector<AnimationClip>&`, and ADR-550's digest already answers the identity question. The
part that makes it more than a five-line change is that `analyse` writes `clipPhases` and
`clipContacts` *parallel to* `clips` and `rootMotion` binds by clip index: per-rig results derived
from what would become shared input.

The other half of §48 is in good shape and is now asserted: per-instance state really is
per-instance — two rigs from the same file have separate storage and posing one leaves the other
untouched.

## §52 — visual regression, numerically

`tests/data/motion-baseline.txt`: 335 named quantities in metres, recorded for the alien under the
full seven-layer stack at sixteen samples of one walk loop — joint positions for six tracked joints,
foot contact heights, and per-sample pose continuity. Compared at 0.1 mm, which is loose enough that
a fused multiply-add does not fail it and five hundred times tighter than the smallest effect
measured in this phase (the 0.052 m foot slide).

Named quantities rather than a checksum, deliberately. A screenshot diff and a hash both answer "is
this different" with a number nobody can act on; this one says *which joint, at which sample, by how
many millimetres*. Falsified by nudging the reach target 1 cm: it named `hand.l.x` at every sample
and printed the delta.

The regeneration escape hatch (`AVGEN_WRITE_MOTION_BASELINE=1`) is necessary and is also how this
kind of test dies — a regression appears, someone regenerates, the diff is a wall nobody reads. The
mitigation is that the file is small, human-readable and in metres, so regenerating puts the change
in front of a reviewer instead of hiding it in a hash.

## §53 — profile before optimizing

The profile was §47's. `PoseLayerStack::ensureModel` now keeps `model_` valid across the layer loop
and recomputes only the joints a previous layer wrote, plus their descendants — found by **diffing
the pose**, not by consulting each layer's mask, because the mask is a claim and a layer that wrote
outside it (which is `docs/testing.md` #25, and has already happened once in this phase) would hand
the next layer a stale model space with nothing to fail.

| | before | after |
|---|---|---|
| full stack, per character per frame | 21.91 µs | **14.1–14.3 µs** |
| 100 characters | 2.16 ms | **1.41 ms** |
| adding one more model-space layer | 2.64 µs | **1.05 µs** (a full walk is 2.10 µs) |

**ADR-603 predicted the stack would approach 4.9 µs, and it did not — it is three times that.** The
first `ensureModel` of each frame follows the clip sample, which changes every joint, so one full
walk per frame is a floor rather than something the optimization removes. Seven walks became one
walk plus six dirty-set passes, and a dirty-set pass is not free. Recorded as ADR-605: the win
reported alone is a success, and the win against a stated expectation is a success plus a corrected
model of where the time goes.

§52 is what makes it safe: the incremental rebuild changed **none** of the 335 baseline quantities,
to 0.1 mm. An optimization to a solver that cannot show its output unchanged is a rewrite.

## §54 — the final pipeline, and who owns what

§54 requires this before Phase B can be declared complete. The pipeline, in the order it runs, with
the owner of each decision named — because most of the defects found in this phase were two owners
answering the same question, or none answering it.

```
CHARACTER INTENT (entity tier — re-simulated on a seek, so it may remember)
  CharacterIntent -> planLocomotion -> MotionRequest
    |
    v
  stepMotion(MotionRequest, MotionState, MotionLimits, dt) -> MotionSolution
    velocity, facing, acceleration, speed, turnRate            [§35, ADR-545]
    |
    v
  LocomotionState  <- THE SEAM. Published by BOTH EntityWorld::update and
                      EntityWorld::seek, field by field           [ADR-554]
    |
================ tier boundary: below here nothing may remember ================
    |
    v
  MotionContext    <- built per frame by driveLayers from the seam, converted
                      into the rig's frame through the node's world transform,
                      because a posed rig has no world position   [ADR-274]
    |
    v
  BASE POSE        <- AnimationPlayer samples the clip (or a MotionProvider
                      produces it) into a rest-seeded Pose
    |
    v
  POSE LAYER STACK, in `poseLayerStage` order, not file order     [§5]
      10 Stride     scales foot excursion to the travelled distance
      20 Lean       leans the spine into acceleration and turn
      30 Secondary  breathing and settle; a pure function of the timeline second
      40 Aim        head/eyes toward the look target
      50 Additive   a reaction played over the base
      60 Foot       plants the feet on the ground plane
      70 Reach      a hand to a target
    each layer:  ensureModel -> read -> write joints -> mark the model dirty [§53]
    |
    v
  BODY COMPENSATION   pelvis moves so the feet can reach          [pre-pass + stage]
    |
    v
  FINAL POSE -> skinning palette
```

**Who owns translation.** The entity tier, exclusively, and there is exactly one authoritative
movement result (ADR-337). Layers rotate; the two that translate — Stride scaling an excursion and
Body Compensation moving the pelvis — do so in the rig's own frame and never in the world's. Root
motion is applied by `applyRootMotionCompensation` *before* the layers, and the two writes are equal
and opposite so the drawn body does not move.

**Who owns rotation.** The layers. `solveTwoBone` returns two model-space pre-rotations rather than
composed local transforms, because the caller is the only thing that knows what a local transform is
on a given rig — and reporting them separately is what lets a partial-weight caller slerp each
towards identity without the knee's share depending on the hip's.

**Who owns contacts.** The Foot layer owns the *solve*; `IGroundQuery` owns the *answer*, and "no
answer" is not "no ground" (ADR-551). The entity owns the smoothing of the ground plane, and the
layer owns the solve against it: everything above `groundPoint` in `LocomotionState` is stateful and
re-simulated on a seek, and everything below it is a pure function of the pose and that plane
(ADR-359).

**Who owns IK.** `scene::ik` owns the mathematics and nothing else. It is a pure function of the
chain it is handed, which is why full extension is a safe default — the next frame does not start
from this frame's answer, it starts from the animated pose. A limb is three *named joints* and a
write-back rule, not an ancestor chain (ADR-543), and the price of that is ADR-601: on a rig that is
not a hierarchy the bone lengths are a property of the frame.

**Layer ordering** is a contract, not a convention (§5). `poseLayerStage` returns the stage;
`rebind` sorts by it with `std::stable_sort`, so two foot layers stay left-then-right. The order is
asserted twice — as an order and as an outcome — with a foot-layer-alone control.

**Data ownership.** Shared: `Skeleton`, `AnimationClip`s, resolved masks, chain definitions,
retarget profiles. Per-instance: `Pose`, `PoseLayerStack` state, `MotionState`, velocity, phase,
targets, seeds. §47 measured that the *time* does not grow with the count (+0.9% from 1 to 100);
§48 measured that the *data* does — 78% of the shipping scene's rig memory is a byte-identical second
copy, and ADR-604 says where the fix belongs.

**Offline / runtime boundary.** Offline: clip analysis (`analyse`, contact tracks, phase tracks),
motion database construction, variant generation and its quality gate (§41–§44), travel
classification (ADR-552). Runtime: everything in the diagram above. The rule that keeps the boundary
honest is that **the offline side may measure the runtime side, and the runtime side may not depend
on having been measured** — an unmeasured metric never fails a gate, which is correct, and the
caller configures the gate, which is what makes it able to refuse (ADR-600).

**The one rule that decides the tier boundary**: the entity tier is re-simulated by
`EntityWorld::seek`, so anything it accumulates is reconstructable at frame N by construction. The
pose tier poses the rig *once* on a scrub, so it may never accumulate — a foot lock anchor is
derived (ADR-557), a layer blend is derived from an elapsed time (ADR-602), and a model-space cache
is invalidated rather than carried (§53).

### Known limitations, stated rather than discovered later

- **ADR-601**: on this rig the solver's reach depends on the frame. Real, sub-pixel at Glowmere's
  framing, 20 px at a character-scale one, deliberately unfixed.
- **ADR-604**: five aliens carry five byte-identical copies of the same 26-clip pack; a hundred
  characters would be 301 MB of which 298 MB is the same bytes.
- **`Gait::footSlip` still has the threshold** ADR-602 describes. The correct driver shape is
  demonstrated in the slice; changing the function itself lands on `assets/farm`.
- The §44 quality limits are a **locomotion** gate, not a clip gate: 13 of the alien's 26 clips fail
  them, which is the expected shape for `Dying_forward` and `Crazy` and a trap for any generator fed
  non-locomotion source.
- The remaining per-frame cost is one full hierarchy walk plus six dirty-set passes (ADR-605).

### Phase C integration points

`IMotionProvider` already splits `advance` from `pose` (ADR-556) so a seek can replay 5,400 steps
and pose once. `MotionChain` already orders Neural → Matching → Clip with fallback. `MotionDatabase`
and `searchMotion` exist with feature vectors and continuity cost. Phase C replaces what produces
the base pose and changes nothing below `MotionContext`.

## §49 — deterministic randomness

My own audit marked this done, and it was wrong. §49's second sentence — "do not use uncontrolled
global randomness" — was satisfied: the secondary layer is a pure function of the timeline second
(ADR-360). Its **first** sentence names two fields, `characterSeed` and `layerSeed`, and neither
existed. Variation was hand-authored, one `phase` per layer per character typed into the scene file.
**Hand-authored spread is not a seed; it is the absence of one, done by hand.** Five aliens can be
spread that way and a hundred cannot.

`seedPhase(characterSeed, layerSeed)` is a bit-mixer — no state, no sequence, no order dependence —
hashed into a phase offset rather than into a generator, with `seedPhase(0, 0)` exactly `0.0f` so an
unseeded layer is byte-for-byte unchanged. `seedFromName` is FNV-1a rather than `std::hash`, whose
value is explicitly allowed to differ between runs of the same program, which would make a
"deterministic" seed reproducible only by accident. Because a knob nobody sets refuses nothing
(ADR-600), `driveLayers` seeds every layer it drives, from **names** rather than indices: an index
changes when someone reorders a scene file, and a render that changes because two characters swapped
places in a JSON array is what §49 exists to prevent. The authored `secondaryPhase` survives as an
offset on top, rather than being replaced when it happens to be zero.

Measured: a hundred seeded characters, closest pair 0.00002 apart, largest empty gap 0.0583 against
a 0.25 bound a corner-piling hash would fail. Recorded as ADR-606.

## §50 — editor / debug visualization

What makes this stage load-bearing rather than cosmetic: every defect this phase found was invisible
until something measured it, and **a probe can only be written once somebody suspects the thing it
measures**. An overlay works the other way round.

Five toggles beside the existing `Skeletons` one in the World panel, drawn in
`buildDebugGeometry` — which is testable without a GPU, so the geometry is read back and asserted
rather than eyeballed:

- **IK chains**, coloured by what the solver said: green solved, amber clamped, red degenerate. The
  colour is the diagnostic; a chain drawn the same whatever the solve returned is a picture of a leg.
- **IK targets**, with a line from the thing asked to reach them, so "the hand is off the target"
  and "the target is not where you think" are different pictures.
- **Contacts**: the plane under each foot and the gap to the foot — absent, not flat, when there is
  no ground, because no answer is not no ground (ADR-551).
- **Motion vectors**: velocity, acceleration and facing, from the state the layers actually ran with.
- **Body compensation**: where the body joint was and where the solve moved it, red when a limb
  still cannot reach — "it ran" and "it worked" are different answers.

Everything reads the **realized** weight, not the requested one: a layer at requested 1.0 and
realized 0.02 is two frames into a blend, and an overlay showing the request would draw §46's defect
as though it were not happening.

`Composition::MotionDebug` gained the numeric half — per-layer name, kind, requested and realized
weight, resolution, IK status — plus mode, phase, ground speed, turn rate and the body correction.
Gathered in the seam rather than in ImGui so it is testable without a GPU and without a panel: the
numbers are the part that can be wrong, and **a panel that renders wrong numbers correctly is not
debuggable, it is convincing.** I cannot see ImGui, so what is claimed here is that the data is
right and the panel is a thin reader of it.

Arms that can fail: each toggle draws nothing when off; moving a reach target out of range recolours
12 of 36 chain vertices and **not** the other 24, so the diagnostic distinguishes the limb that
clamped from the two that did not; and `hasGround` false draws nothing at all rather than a plane
at zero.

## §51 — the audit against the matrix

Twenty-four rows in §51's own list. Eighteen were already covered and are cited by file and test
name in `tests/unit/test_phase_b_matrix.cpp`'s header. Six were gaps, and two of the six were
findings rather than formalities:

- **Contact release.** Before §46 a released contact dropped the foot **0.200 m in one frame** — the
  same defect §46 found on the reach layer, on a different layer, missed by the six-cause list
  because the slice's script never released a contact. §46's blend fixes it structurally, which is
  the argument for having put it on `PoseLayer` rather than in the reach code: measured now at
  0.0231 m worst frame over a 0.25 s release.
- **Lowered ground.** My row asserted symmetry and failed. Raising the ground 0.20 m moves the foot
  0.1999 m; lowering it 0.20 m moves the foot 0.0119 m. The engine is right: the alien binds with
  its leg nearly straight, so there is no slack to extend downward and the solve clamps. **A foot
  layer alone cannot follow ground that falls** — that is what body compensation is for. The
  expectation was wrong, not the code.

The other four: elevated ground, a moving reach target (worst tracking error 0.00000 m, worst hand
step 0.0022 m over 121 frames), and secondary motion **bounded** (1.4993° against an authored 1.5°
over six minutes) and **stable** (bit-identical one period later, and different half a period later,
so the check is about the period rather than about the layer doing nothing).

The bounded/stable probes first read **exactly 0.00000 m** because they measured the head's
*position* on a flat rig, where rotating a sibling moves nothing. That is `docs/testing.md` #26, and
the second confident zero this rig has produced in one phase.

# Phase C

## §6 and §17 — the database's memory and the scan's cost, measured before anything is built on them

ADR-606 is the standing rule for a phase that arrives with sixteen sections already marked done,
and it paid on the first pass. `MotionDatabase` genuinely satisfies §4, §5, §7, §8, §9 and §13 —
struct-of-arrays with indices rather than poses, versioned, `mean`/`scale` normalization,
data-driven `MotionFeatureConfig`, a tag bitset. It even carries `featureBytes` and `metadataBytes`,
which is what made §6 look done. **Nothing had ever exercised them at scale.** The alien's 26 clips
come to a few thousand samples; §6 names a million, and the three scales it names are the
deliverable. *A field that reports a number is not a measurement of that number.*

Feature dimension 33 floats = 132 bytes, plus five 4-byte per-sample fields = **152 bytes/sample
exactly**, asserted as arithmetic rather than as "roughly linear" — which is what would catch
somebody adding a `std::string` or a `Pose` to the per-sample data, the failure §5 and §6 exist to
prevent.

| samples | features | metadata | total | per sample |
|---|---|---|---|---|
| 10,000 | 1.26 MB | 0.19 MB | 1.45 MB | 152.0 |
| 100,000 | 12.59 MB | 1.91 MB | 14.50 MB | 152.0 |
| 1,000,000 | 125.89 MB | 19.07 MB | **144.96 MB** | 152.0 |

§17's benchmark, taken **before** §16's two-stage search rather than after, for ADR-603's reason —
a number written down beforehand can be wrong, and an optimisation whose starting point was never
recorded cannot be shown to have helped:

| samples | µs per query | queries per 60 Hz frame |
|---|---|---|
| 10,000 | 360.8 | 46 |
| 100,000 | 3,627 | 5 |
| 1,000,000 | **36,492** | **0** |

**One query at a million samples takes 36.5 ms — more than two whole frames, for one character.**
Scaling is 101x for 100x the samples, so the scan is honestly linear and the linear scan is the
right first implementation and the wrong last one. That is §16's justification, measured rather than
asserted.

The benchmark asserts it actually scanned: every sample carries the required tag, so `considered`
equals the sample count and `rejected` is zero. A filter that had quietly emptied the candidate set
would otherwise have produced a very fast and completely meaningless number.

**And the cross-phase catch.** §4 says the database "should be … shared between character
instances", which is the kind of clause that reads as tidiness. At 144.96 MB it is not: a hundred
characters each holding one is 14.5 GB. Phase B §48 found exactly this failure one tier up —
`SkinnedRig` holds its `Skeleton` and every `AnimationClip` by value, and 78% of the Glowmere
scene's rig memory is a byte-identical second copy (ADR-604) — and it would be invisible to §47's
timing here for the same reason it was there. `MatchMotionProvider` holds a `const MotionDatabase*`,
so it is correct today; a hundred providers are now asserted to point at the same `features.data()`,
because *"it is a pointer today" is not a property anything checks.*

## §15, §17 and §19 — the correction, and what survives it

Re-reading §15 and §17 after writing the §6 benchmark caught that benchmark, an hour old, by the
author who had just written ADR-606 about exactly this. §15 demands **real** motion distributions as
the primary benchmark and mine was synthetic; §17 names **1,700 frames**, **average** and
**worst-case** latency and **build time**, and mine started at 10,000 and reported only a minimum.

What survives is the useful part: a linear scan touches every sample whatever the values are, so
its cost is distribution-independent and the synthetic timing is valid *as a measurement of a
linear scan*. Filtering effectiveness and first-stage recall are not distribution-independent at
all. **Synthetic data may time the scan; only real data may judge a filter or a first stage.**

The real Glowmere database, built from `alien-scout.glb` through `buildMotionPack` and
`buildMotionDatabase`:

- **26 clips, 1,738 samples, dimension 33** — §17's "1,700 frames" is the actual content, not a
  round number, which says real-data-first is the intended reading rather than a caution.
- **0.252 MB at 152.0 bytes/sample**, the same per-sample arithmetic as the synthetic table, which
  cross-checks both.
- Build: **32.7 ms** for the pack, **77.2 ms** for the database.
- Query latency over 500 queries seeded from inside the real distribution: **best 8.88 µs, average
  9.25 µs, worst 17.46 µs** — worst about twice average, and **955 characters per frame at 60 Hz on
  the worst case**.

**So §16's two-stage search is justified by the million-sample case and by nothing in this
repository today.** At present content the linear scan is comfortably correct. Saying so is what
stops the two-stage search being built for the wrong reason and then defended with a benchmark that
never needed it.

The benchmark's control: a query seeded with sample 579 returns sample 579 at cost 0.000000.
Without it every latency number could have been the cost of a fast refusal.

## §10 — the cost function, and seven weights that weighted nothing

§10 was among the sixteen sections Phase C inherited as done, and `MotionFeatureConfig` carries
exactly the seven weights §10 asks for — which is what made it look met.

**Five of the seven were read by nothing at all.** `jointPositionWeight`, `jointVelocityWeight`,
`trajectoryPositionWeight`, `trajectoryFacingWeight` and `rootVelocityWeight` appeared in no
translation unit outside their own declaration. The two that *were* read, `phaseWeight` and
`contactWeight`, were used only as `> 0` presence tests deciding whether to include a dimension:
setting one to 2.0 rather than 0.5 changed nothing at all. The search summed every dimension with
weight 1, so the cost was a single undifferentiated squared distance — **precisely the opaque
scoring function §10 names, with a tuning surface bolted to the outside of it that did nothing.**

That is ADR-558's family, a control that does nothing, and it is worse than an absent control
because an absent one is obviously absent.

Fixed by deriving a per-dimension weight vector from the config and applying it in the scan.
Deriving rather than baking is the load-bearing choice: **a weight is now tunable without
rebuilding the database**, because the features are unchanged and only what they are multiplied by
differs. `motionFeatureLayout` states the dimension layout once, so the weights and the breakdown
cannot drift from what `buildMotionDatabase` writes.

§10's other half — "avoid an opaque scoring function" — is `MotionCostBreakdown`, computed **once
for the winner** rather than accumulated per candidate: per candidate it would cost seven
accumulators on every one of a million samples to produce a number thrown away for all but one, and
the early-out means a losing candidate's partial sums would be wrong anyway.

The probe is built so it fails if the weights ever go inert again: two candidates are made wrong in
*different groups* by the same amount, so they tie at equal weights, and raising one group's weight
must flip the answer — impossible unless the weight is read. Both directions are asserted, so it is
a test of the weights rather than of a tie-break, and nothing touches `db.features` between the
three searches, which is what demonstrates the tunable-without-rebuild property. Shown failing by
forcing the weights off: four assertions fail, which is the state the code was actually in.

The breakdown asserts `total() == cost`, so it cannot become a decorative second opinion that
drifts from the number actually used to choose.

**§11 and §12, audited and recorded rather than changed yet.** Continuity is binary: a candidate
that is literally `sampleNext[current]` pays nothing and *everything else* pays the full penalty —
so the sample two frames later in the same clip is penalised exactly as hard as a sample from an
unrelated clip. §11 lists "current sample, previous sample, source clip, phase, root velocity,
transition distance" as inputs, and a next-or-not flag is the crudest possible reading of that.
§12 is met in the respect it cares about — it uses tag metadata, never clip names, which is its one
explicit prohibition — but is likewise binary. Both are next.

## §16 — the two-stage search, and what recall hid

**The disclaimer first**, because it belongs with the justification: on this repository's content a
linear scan is comfortably correct — 1,738 samples, 17.46 µs worst case, **955 characters per frame
at 60 Hz**. §16 exists for the scale §6 and §17 ask about, where one query is **36.5 ms**. A future
reader finding a two-stage search here should not conclude the linear scan was inadequate.

Swept on **real** Glowmere motion, 249 queries drawn from inside the distribution and perturbed so
the answer is not trivially the seed. A typical candidate on this database scores 69.72 worse than
the best one, which is the denominator that makes an excess mean anything:

**Corrected.** The percentages below were first published against an *unweighted* cost spread of
69.72; the feature-distance sweep found that denominator was in the wrong space and the correct,
weighted value is **52.57**. The excesses are unchanged — they are differences of weighted search
costs and only the scale moved — so every percentage rises:

| plan | recall | worst excess | **× the typical gap** | samples fully scored |
|---|---|---|---|---|
| stride 8, prefix 12, top 32 | 94.4% | 60.60 | **1.15×** | 2320 |
| stride 8, prefix 12, top 128 | 99.6% | 48.93 | **0.93×** | 1873 |
| stride 8, full prefix, top 32 | 96.8% | 59.49 | **1.13×** | 2278 |
| **stride 4, full prefix, top 32** | **99.6%** | **2.19** | **0.04×** | **84** |

Reported as a **multiple**, not a percentage. A column headed "% of spread" reading 115.3% looks
like a bug to any reader meeting it cold, and the explanation lived in a document rather than at the
number — the exact thing three remedies tonight converged on fixing. A multiple carries no
implication of a ceiling.

**The finding got stronger, and the headline changes.** Three of the four plans have a worst-case
miss that **exceeds the entire good-to-typical spread** — so the miss is not merely "as bad as a
random candidate", it is **worse than a typical candidate**.

**A percentage over 100 is legitimate here and not a bug**, which has to be said or a reader will
assume arithmetic error. `costSpread` is a *typical-minus-best* gap: the mean, over probes, of how
much worse an arbitrary distant sample scores than the best available. **A worst case is not bounded
by a typical case.** A search that misses badly can land on something worse than the arbitrary
sample the scale was built from, and three of these plans do.

**The recommendation is unchanged**, which matters so nobody reads a correction as a reversal:
stride 4 at full prefix survives at 4.2%, and it was and remains the plan to pick.

**94.4% recall hides that the misses land 86.9% of the way to a random sample.** That matcher does
not pick a slightly different frame of comparable motion; it picks different motion. And the
99.6%-recall plan at stride 8 is *still* 70.2% when wrong — **raising recall did not make the
failures benign.** ADR-559's family in a new place, and the opposing quantity is severity.

Getting the denominator right took three attempts, and the wrong two are recorded because they are
the instructive part: `staged/full` reported **2957x**, because a query drawn from the database sits
near a sample so the best cost is near zero — *a ratio whose denominator can approach zero measures
the denominator*. Excess against the mean best cost was the same defect one step removed. Only the
measured gap between a good match and a typical one is a scale on which "how bad is this miss" has
an answer.

**And the cheap prefix bought nothing.** The textbook design ranks coarsely on a prefix; here the
full-prefix plan at stride 8 was barely better than the 12-dimension one. The entire win is the
stride, and the safe plan is stride 4 at full prefix: **84 samples fully scored against 1,738, a 20x
reduction at 99.6% recall and a 3.1% worst case.** Recorded as ADR-609.

## §11 — graded continuity, measured and rejected

§11 names its own metric: *"constantly jump between unrelated clips"* is a **rate**, so it is
measured by running the matching loop, not by scoring a query.

| continuity | clip jumps/s | mean index step | stalled | worst excess |
|---|---|---|---|---|
| binary | 0.50 | 2.47 | **0%** | 0.0612 |
| graded 0.5–4.0/s | 0.50 | 1.81 | **13%** | 0.0612 |

**Identical jump rate at every rate tried, and the loop stands still on 13% of steps.** The reason
is structural: a penalty proportional to the distance from the current sample is *zero for the
current sample*, so not moving is free and the matcher freezes rather than continuing. A freeze is
the worst outcome wearing the appearance of the best, because a frozen matcher also reports zero
jumps. Kept at its inert default with the measurement recorded (ADR-610), so a second attempt can
see it was tried and why it failed — a working version must penalise *not advancing*, which needs
§11's "previous sample" input as well.

**The fixture was vacuous first.** It queried with `sampleNext[current]`, so the natural
continuation was always the answer and continuity never decided anything; it reported **0.00 jumps
per second for every configuration**. Caught by this repository's own rule that an exact zero is a
reading to distrust. And the vacuous version **carried the evidence of the freeze all along** —
index step 0.15 against 1.82 — in a test whose headline metric said everything was fine. A probe
that cannot fail does not merely prove nothing; it can present a real fault as a success.

## §12 and §14 — the transition term does something, and filtering removes 82%

**§14, whose deliverable is "measure how much it helps", measured on the real database:**

| | scored | rejected | time |
|---|---|---|---|
| unfiltered | 1738 | 0 | 9.50 µs |
| require `Locomotion` | 307 | **1431 (82%)** | **4.08 µs (2.33x)** |

The tag distribution is reported first, because **a filter's value is a property of the corpus, not
of the filter** — on a pack where every sample carried the same tag it would be exactly zero however
well written it was. Locomotion 18%, idle 14%, walk 15%, run 3%, turn 7% — **and cyclic, travelling
and oneshot are carried by zero samples.** Three tags with no writer, which is ADR-608's family seen
from the other side, and worth knowing before anything is built that filters on them.

**§12** is correct in its one explicit prohibition — it keys on tag metadata, never on clip names —
and it is now checked the way ADR-608 says every configured term must be: make it large and see the
answer change. With the transition weight at zero the idle wins on features alone; at 0.5 the walk
wins. Continuity is pinned to zero in both arms so only the transition term differs.

**And the winner's breakdown reports a transition cost of zero, correctly.** The penalty did its
work on the candidate that *lost*; the chosen sample stayed in the family and paid nothing. A
breakdown answers "what did this cost", not "what changed the decision" — **a term can be decisive
and read as zero** — so the penalty is confirmed separately on a query whose surviving choice does
have to cross. Recorded in ADR-611 as a corollary, because anyone debugging a choice through §50's
read-out will hit it.

## §13 — the tags, and a seam that dropped one

§14's tag distribution raised the right question, and it is not whether the vocabulary is
well-formed: **for each tag, is there a writer, a reader, or only one end of the contract?**

| tag | writer | in the database | verdict |
|---|---|---|---|
| Locomotion, Walk, Run, Idle, Turn | clip names / authored tags | 18% / 15% / 3% / 14% / 7% | connected |
| **Cyclic** | `analysis.phase.cyclic` | **0% → 95%** | **was a broken seam** |
| Travelling | `analysis.travels` | 0% | unreachable, correct verdict for the wrong reason |
| OneShot | `!clip.loop` | 0% | **no writer on this path** |

**`Cyclic` was the real finding, and it was better than "no writer".** `buildMotionDatabase` called
`analyseClip(skeleton, clip, {}, 0, {})` — with an **empty contact-joint list** — so the phase
analysis had no foot plants and `cyclic` came back false for every clip of every pack. Meanwhile
`buildMotionPack` had already computed it correctly, with the real contact joints, and stored it:
**25 of the alien's 26 clips are cyclic in the pack, and zero samples carried the tag.** The
database threw away a correct answer and recomputed it with the inputs missing.

It now reads what the pack stored — one statement of a fact, several readers, the rule
`motionFeatureLayout` already follows — and the tag goes **0% → 95%**. A filter on `Cyclic` would
not have sat inert; it would have emptied the candidate set and returned a confident answer computed
over nothing.

**`OneShot` genuinely has no writer here.** `PackClip::loop` defaults to `true` and is assigned in
exactly one place — deserialising a pack from JSON — so a pack built from a rig has every clip
looping, `Dying_forward` included. Recorded rather than fixed: deciding whether a take loops is a
judgement about content and belongs with the clip analysis, not bolted onto the tagger.

**`Travelling` stays 0, and it is the right answer for the right reason — my first write-up of this
said otherwise and was wrong.** I claimed the tag was unreachable because the database passes an
empty contact list. It is not: `analyseClip` computes `travels` from `measureRoot`, whose inputs are
the travel joint's translation track and the rest height, and whose verdict is
`extent > restHeight`. Contacts are not an argument to it; the only thing `ContactSettings{}`
contributes is a 30 Hz sample rate, which is the alien pack's authored rate. **The tag is reachable
and working**, and reads 0% because ADR-540 holds: every locomotion clip here is authored in place,
and the five clips whose root moves more than 5 cm are deaths that fall far short of a rest height.

That correction matters more than the fact. *"Travelling remains unreachable"* was a claim in the
codebase that no test could disagree with — ADR-385, and the same shape as the dead weights and the
one-ended tags. The next reader concludes the tag is vestigial and deletes it, or works around a
mechanism that already works.

What *is* wrong there is the cost: the call runs `detectContacts` and `extractPhase` over every
clip, discards both, and keeps one boolean. The clean fix is the one `phase` just received — have
the pack store the verdict it already computed — and it is noted for §37's offline/runtime boundary.

## §18 and §19 — the database reproduces its own content

§18 puts a number on the corpus — "only ~1,712 frames … useful for correctness, **not** sufficient
to prove motion matching quality" — which is the measured 1,738 again, and the third time C's text
has quoted this repository's own content back at it.

§19's one falsifiable clause is that **the database should be able to reproduce existing animation
behaviour**, and it is the clause everything downstream assumes silently. Measured by driving the
matcher along each clip the database was built from, querying with the *next* frame's features
perturbed so no query lands on a sample, and judging the loop on whether it stays on the clip:

- **19 clips long enough to follow, mean 100.0% of steps stayed on the clip**, worst-case drift 0
  frames — the matcher reached the exact sample asked for, every step, on every clip.

**The companion assertion was wrong in the instructive direction.** I first asserted
`worstDrift > 0`, where drift is the *error* between the sample matched and the sample wanted — so
it demanded the matcher be imperfect, and failed on a run that tracked every clip exactly. A
companion metric must count that **something happened**, not that something went wrong; those are
opposite quantities, and they are easy to confuse precisely because ADR-611 is about metrics that
cannot see failures. The correct companion is that the loop *advanced* — a new sample on 1.00 of
steps — so 100% on-clip cannot be earned by a matcher returning one sample forever.

**§20 is blocked on data this repository does not have.** It requires a 100STYLE subset, and §18 is
explicit that third-party licensing must be verified rather than assumed and that dataset terms are
not interchangeable with code terms. Downloading and repackaging an external corpus is not something
to do on an assumption, so §20 is recorded as blocked on a licensing check and an import rather than
attempted with substitute data — which would measure the substitute.

### A note on Phase C's illustrative figures

Three of C's sections quote numbers that turn out to be this repository's own content, each slightly
stale: §17's "1,700 frames" and §18's "~1,712 frames" against the measured **1,738**, and §6's round
ladder against a real per-sample size of 152 bytes. **The specification was written with this corpus
in view, so its figures are stale measurements rather than targets.** Anyone treating one as a
requirement will be tuning to a number nobody measured. The useful reading is the opposite one: when
a spec's example matches the repository's actual content, the example is evidence about intent —
here, that real-data-first was meant literally.

## §20 — blocked, and what that blocks knowing

§20 needs a 100STYLE subset. §18 is explicit that third-party dataset licensing must be **verified**
rather than assumed and that dataset terms are not interchangeable with code terms, so downloading
and repackaging an external corpus on an assumption is precisely what that instruction exists to
prevent — and substitute data would measure the substitute (§15). It needs a licensing check and an
import decision from the owner.

**Recorded as a known gap in what has been proven, not as a section skipped.** §20 is a *scale*
experiment, and its value is entirely that a large, diverse corpus breaks assumptions a
1,738-sample one cannot. Several results already on the record are properties of *this* corpus and
§20 is the section that would say which of them survive:

- the **stride-4, full-prefix** search plan, and the finding that the cheap prefix bought nothing;
- the **82%** rejection rate of a `Locomotion` filter, which is a property of the tag distribution;
- the **69.72** cost spread that every severity number in §16 is measured against;
- the **95%** `Cyclic` share, on a corpus where 25 of 26 clips are cycles.

None of those is wrong. All of them are *local*, and until §20 runs that is what the record should
say.

## §21 and §22 — the audit, and a gap-finder that could not find a gap

**§21's audit**: C names seven augmentation kinds — mirroring, speed, stride, directional warping,
turn variation, start/stop variants, root-motion adaptation. Phase B's `VariantKind` has **four**:
`Source`, `SpeedWarp`, `StrideWarp`, `Mirror`. Directional warping, turn variation and start/stop
variants are absent; root-motion adaptation exists separately as `adaptRootMotion`. Recorded here
because "B already has variants" is precisely the audit-from-memory ADR-606 is about.

**§22's audit**: C names six coverage axes. B's `measureCoverage` takes a list of target **speeds** —
one axis. So `scene::motion_coverage.{hpp,cpp}` is new.

**The denominator was chosen before measuring**, and it is the design decision. The obvious reading
of "coverage over six axes" is a six-dimensional grid, and it is wrong: at eight bins per axis that
is 262,144 cells, which 1,738 samples can occupy at most 1,738 of — **under 0.7% for any corpus of
this size, however complete**. It would measure the dimensionality, not the content. So coverage is
reported **per axis, marginally**, plus the one pairing where a gap means something concrete.

**Then the instrument failed its own calibration, which is the finding.**

| bins | empty bins across six axes | speed axis |
|---|---|---|
| 8 | **0** | 100.0% |
| 32 | 7 | 78.1% |
| 128 | 88 | 48.4% |
| 512 | 788 | 33.0% |

At the default eight bins **every axis is 100% covered with no gaps** — and that is a fact about the
bin count, not the corpus. It takes only as many distinct values as there are bins to fill an axis,
which 1,738 samples of varied motion supply trivially. **A gap-finder that cannot report a gap for
any plausible corpus is ADR-182 in the instrument built to find absences**, and it was caught by the
standing habit of distrusting a perfect score.

**And the joint occupancy was carrying the information all along: 38 of 64 (speed × turn) cells —
a 26-cell gap that the marginal report called 100% covered.** That inverts the framing the analyzer
was written with. The *pairing* is the informative measure; the marginals are the near-vacuous one,
useful only as a check that every axis is populated at all. The pairing is reported as a **count**
rather than as a sixth percentage, so it cannot be averaged into the others and lost.

### §22 closed: what justifies the bin count

The calibration table has a failure at **each** end and the first pass named only one. At 8 bins the
instrument cannot report a gap. At 512 bins it reports 788 empty bins — but 1,738 samples over 3,072
marginal cells would leave hundreds of holes in a corpus that covered its space perfectly, so most
of those are **sampling sparsity**. That reading is exactly as untrustworthy as the first and looks
better, because it reports gaps.

A round number defends against neither. So the width is derived from **a difference the matcher can
act on**, measured rather than assumed: *how much must the requested speed change before the search
returns a different sample?* Answer, over 33 probes on the real corpus: **1.083 m/s**, which
justifies about **three** bins over a 0–3 m/s axis.

Checked before trusting it — the root forward velocity spans **−2.68 to 1.72 m/s**, a 4.4 m/s
spread, so the feature is live and the coarseness is not a dead dimension. It is **weighting**: root
velocity is 3 of 33 dimensions and the pose terms swamp it. Now that §10 has made the weights
actually do something, that is a tunable with evidence behind it rather than a guess — but tuning it
needs a quality metric, so it is recorded rather than changed.

**Conclusion: the marginal report is demoted to what it is.** Not coverage — a populated-ness check
that every axis has motion somewhere along it. `report()` now says "populated" rather than printing
a percentage that reads like coverage, and carries the reason inline. **`jointOccupancy` is the
informative measure**, and it is a count.

### §21 and §22 do **not** agree, and the correction is the result

I wrote here that the 26 empty (speed × turn) cells were largely *turn* coverage, and that the
coverage analyzer had independently confirmed §21's missing turn-variation augmentation — two
sections arriving at the same hole from opposite directions.

**That was measured at eight bins, a number nothing justified.** Once the width is derived from what
the matcher can distinguish, the grid is 3×3 and **9 of 9 cells are occupied**. The 26-cell gap was
an artefact of the bin count. The agreement was between an audit and a measurement taken at an
arbitrary resolution, which is not agreement.

**The honest result is different and more useful: coverage analysis cannot motivate augmentation on
this corpus**, because every distinction the matcher can make is already populated. The case for
building turn variation now rests on §21's audit against C's text — which still stands — or on a
corpus large enough to have gaps at this resolution, which is §20.

The instrument can still see absence when there is absence: pinned to 128 bins it reports gaps. That
is what distinguishes "no gaps at this resolution" from "cannot report a gap", and it is asserted,
because without it the result above is indistinguishable from a broken analyzer.

### The resolution is derived per run, not stored

The width is calibrated against the **weight vector**, and §10 made those weights mutable for the
first time — before that they were read by nothing. A stored resolution would be invalidated by the
first person to tune them, silently. That is ADR-389: a coefficient tuned against a quantity is
invalidated by a change to that quantity's distribution.

So `MotionCoverageOptions::bins` defaults to **0, meaning "derive it from the matcher in hand"**. It
costs a few dozen probe searches per analysis, and it makes the resolution a *reported property of
the run* — `bin width derived from THIS matcher: 0.876 m/s` — which tells the reader how finely this
matcher can be interrogated today rather than how finely one could be at some point in the past.

## §24 — the horizons, experimentally validated

`defaultBipedConfig` uses {0.2, 0.4, 0.6} s with a comment citing "the spacing the literature
converges on". §24 asks for the measurement in as many words, so that comment was ADR-385 sitting in
a default this whole phase is built on. Leave-one-out retrieval on the real corpus — query with a
sample's true successor's features, perturbed, `current` deliberately unset so continuity cannot
hand over the answer:

| horizons | dim | bytes/sample | retrieval | query µs |
|---|---|---|---|---|
| none | 21 | 104 | 83.5% | 9.82 |
| {0.2} | 25 | 120 | 88.0% | 10.33 |
| **{0.2, 0.4, 0.6}** — shipping default | 33 | 152 | **93.0%** | ~10.9 |
| {0.1, 0.2, 0.4, 0.8} | 37 | 168 | 92.4% | 11.34 |
| {0.2, 0.4, 0.6, 0.8, 1.0} | 41 | 184 | 91.1% | 11.84 |

**The shipping default wins, and the claim I can support is the weaker one.** Retrieval is computed
over ~158 probes, so the standard error on a 93% rate is about 2 points — which means the 93.0 /
92.4 / 91.1 decline across 33, 37 and 41 dimensions is *suggestive and not significant*, and "more
horizons make it worse" is more than the data carries.

**What the data does carry is decisive on its own: more horizons buy no measurable gain and cost
more.** The cost side is exact rather than sampled — 37 and 41 dimensions against 33, 168 and 184
bytes per sample against 152, 11.34 and 11.84 µs against ~10.9. That is §24's own warning ("do not
assume every dimension improves quality") demonstrated without needing a noise analysis.

The result is *reported* and only weakly asserted: asserting that the current value is best would be
the conclusion writing the experiment, and the same scepticism has to apply to the decline.

## §25 — a trajectory predicted from a MotionRequest alone

**The audit finding first, because it is why the file exists.** `entity::sampleTrajectory` already
existed and looked like §25. It takes a span of **waypoints**, which is a navigation product, and
§25 explicitly forbids requiring one — the matcher needs a trajectory every frame for every
character and most of them are not following a route. *"A trajectory predictor exists"* was true and
answered a neighbouring question.

`entity::predictTrajectory` takes a `MotionRequest`, a `MotionState` and `MotionLimits`, and
integrates forward with **`stepMotion` itself**. That is the load-bearing choice: the prediction
agrees with what the body will actually do rather than being a second opinion about it. A prediction
that disagrees trains the matcher on motion the character cannot produce, and the symptom would be a
character that consistently selects clips it then fails to follow — which is the kind of fault that
gets diagnosed as "the clips are wrong".

Measured:

- **Agreement**: predicting 0.6 s ahead gives `(0.3883, 0.7243)`; stepping the controller 0.6 s for
  real gives `(0.3883, 0.7243)`. Compared against a loop written independently in the test, so this
  is not a function agreeing with itself, and the 1e-5 margin catches any divergence — including
  someone later "optimising" the prediction into a closed form.
- **Limits respected**: asked for 4 m/s from rest, the prediction puts the body at **0.14 m** after
  0.2 s where a straight line at the desired velocity would say **0.80 m** — 5.7x. The obvious cheap
  prediction is wrong, not merely approximate.
- **Lightweight, with a number**: **0.955 µs per call**, ~17,450 characters per 60 Hz frame, so a
  hundred characters spend under 0.1 ms of their budget on it. §25 says "should be lightweight",
  which is a claim with a number behind it or it is nothing.

Two horizons falling inside one tick both get a point, because dropping one would leave a zero where
a position belongs in the feature vector — a silent hole in the thing the matcher searches on.

## §23 — the database quality report

§23 lists eleven candidate metrics, and the audit question is **which of them this repository can
answer from the database** rather than by re-deriving from the source clips — because §13 has just
shown what re-deriving costs when the inputs are not the ones the pack used. So the report is
deliberately narrow, and **says what it does not cover and where that is covered instead**:

```
motion database quality: 1738 samples x 33 dimensions
  duplicates            129 (7.42%)
  nearest neighbour     mean 1.8493, max 11.4375 (normalised units)
  dead dimensions       0 of 33
  matcher resolution    0.876 m/s of speed changes its answer
  speed x turn cells    9 of 9 occupied
  NOT covered here: foot sliding, contact quality and joint limits need a pose, which
  a feature vector does not contain. Those are Phase B's measureMotionQuality, run at
  pack build time on the clip and the skeleton.
```

**That 7.42% was a property of 0.05, and 0.05 was a number I chose.** Corrected: the radius is now
derived the way §22's bin width is — the distance a query must move before the search returns a
different sample, which is the only definition of "duplicate" that licences deleting one. It is
**1.3945** normalised units, twenty-eight times larger, and *below* the corpus's own mean
nearest-neighbour distance of 1.8493.

| radius | duplicates |
|---|---|
| 0.0100 | 6.44% |
| 0.0500 | **7.42%** ← the number I first reported |
| 0.5000 | 26.52% |
| **1.3945** ← derived from this matcher | **57.02%** |
| 2.0000 | 66.11% |

The curve is printed beside the figure so nobody mistakes one point on it for a property of the
corpus.

**And the correction inverts what the number is evidence for.** At 7.42% it read as *"delete 129
redundant samples"*. At 57% it is not a deletion argument at all — deleting half a corpus because
the current weight vector cannot resolve it would be **destroying content to flatter an
instrument**. It is evidence that the corpus is far denser than this matcher can *use*, which points
at the feature weighting (§22 already found root velocity is 3 of 33 dimensions and swamped by the
pose terms) or at the sample rate. The radius carries the same ADR-389 property as the bin width: it
moves when the weights move, so it cannot go stale.

Worth noting the low end is flat — 6.44% at 0.01 against 7.42% at 0.05 — so there really is a small
core of near-exact duplicates across clips, independent of any radius. That part of the original
finding survives; the headline did not.

**The duplicate measure excludes consecutive frames of the same clip**, which is the difference
between measuring the corpus and measuring the sample rate. At 30 Hz adjacent frames are nearly
identical by construction, so counting them would make the duplicate percentage *rise when the
sampler got finer* — a number that describes the sampler, not the content. Same denominator
discipline as §14 and §22.

**Zero dead dimensions**, which independently confirms the §22 finding: the matcher's coarseness on
speed is weighting, not a feature that fails to vary.

The coverage figures are **cited from `measureMotionCoverage`, not recomputed**, so the report and
the analyzer cannot disagree about one corpus — §13's lesson applied one section later, and
asserted. Both human-readable and machine-readable output exist, as §23 asks, and the JSON is
checked for being parseable rather than merely present (including that it contains no `nan`, which
is not JSON).


## One fact, two instruments — and what it predicts

§16's stride-4 plan scores **84 samples of 1,738 at 99.6% recall with a 3.1% worst case**, and I
wrote that up as "the entire win is the stride". §23's derived radius says **57% of samples are
interchangeable to the search**, and its radius of 1.3945 sits *below* the corpus's own mean
nearest-neighbour distance of 1.8493 — so the distance at which this matcher stops telling two
samples apart is on the same order as the typical spacing between neighbours. The corpus sits at or
below the matcher's resolution limit nearly everywhere.

**These are not two results agreeing. They are one property measured twice.** If most samples have a
near-equivalent, subsampling by four cannot lose much, because three of every four skipped have a
stand-in among those kept. Presenting them as corroboration would repeat the §21/§22 mistake exactly
— the retraction there was for treating two views of one arbitrary resolution as independent
evidence, and this would be treating two views of one real property the same way.

Stated correctly it is an **explanation** rather than a confirmation, which is worth more: §16's
headline stops being an empirical curiosity and acquires a mechanism — *the two-stage search works
this well here because the database is oversampled relative to what the matcher can resolve.*

**And a mechanism predicts.** Either a corpus the matcher can actually resolve (§20) or a weight
vector that stops root velocity being 3 of 33 swamped dimensions (§22's finding) should **degrade
the stride-4 plan**. That is falsifiable, it is the first prediction this phase has made about
content it does not have, and it is a better reason to want §20 than any of the six results already
queued behind it.

## A category, not a list of gaps

Three sections of Phase C have now had the same shape, and it is worth naming because it is the form
this codebase's gaps actually take:

| section | mechanism | measurement |
|---|---|---|
| §14 candidate filtering | tag filter, working | "measure how much it helps" — never done |
| §24 trajectory horizons | `trajectoryTimes`, working | "experimentally validated" — a literature citation instead |
| §27 search frequency | `searchInterval`, working | "do not assume every-frame search is necessary. Benchmark." — never done |

**Not absent features. Features shipped without the measurement that would say whether their
defaults are right.** ADR-608's dead weights are the extreme case — mechanism absent *and*
measurement absent, with a declaration that made both look present — but the ordinary case is this
one, and it is invisible to any review that checks whether a thing exists.

## §26–§29 — the loop benchmarked, and a dial that another dial shadows

The mechanisms all existed; the measurement did not. Running the loop as a whole — 600 frames of a
request that changes its mind, driven through `advance` as the product drives it:

| `searchInterval` | searches | switches | held | µs/frame |
|---|---|---|---|---|
| 0.000 s | 150 | 150 | 0 | 2.47 |
| 0.033 s | 150 | 150 | 0 | 2.46 |
| **0.100 s** (shipping) | **150** | **150** | 0 | **2.46** |
| 0.250 s | 120 | 120 | 0 | 1.99 |

**`searchInterval` does nothing at its shipping default.** Identical search counts and within 1% the
same time from 0 to 0.1 s. A search needs `due && !locked`, where `due` is
`sinceSearch >= searchInterval` (0.1 s) and `locked` is `sinceSearch < minimumContinuation`
(**0.2 s**) — the lock is always the later of the two, so it decides every time and the interval
cannot affect anything below it. Only at 0.25 s, above the lock, does it take over.

That is **ADR-608's family with a twist: not a dial nobody reads, but a dial that is read and then
shadowed by another one.** Setting it produces no change, so a person tuning search frequency
concludes it does not matter — the same misdiagnosis cost as the dead weights, from a different
mechanism. It also means **§27's question is unanswerable as the code stands**: the benchmark C asks
for cannot distinguish "every-frame search is unnecessary" from "the interval is not in control".

| `switchMargin` | searches | switches | heldByMargin |
|---|---|---|---|
| 0.00 | 150 | 150 | 0 |
| **0.05** (shipping) | 150 | **150** | **0** |
| 0.50 | 150 | 150 | 0 |
| 100.00 | 150 | 135 | 15 |

**The hysteresis holds nothing at its shipping value, and every search changes the motion** — which
is exactly the thrashing §28 exists to prevent, happening at the defaults. Two readings are possible
and this benchmark cannot separate them: the margin is far too small for this cost scale, or a
changing request genuinely warrants a change every time. §23's measured cost spread makes the first
far more likely — **a margin of 0.05 against a spread of 69.72 is four parts in ten thousand**.
Recorded rather than retuned, because tuning needs a motion-quality metric this phase does not have
and §22 says this corpus cannot supply.

§29 is the one dial in control: minimum continuation 0 s gives 300 searches, 5 s gives 6. Asserted
with its companion — the unlocked loop actually moved — so a frozen loop cannot pass by reporting no
switches (ADR-611).

**All three findings are the same category**: mechanisms shipped without the measurement that would
say whether their defaults are right. Two of the three defaults turn out to be inert.

### §27 is not answered, and §28 was in the wrong units

**§27's honest state is *not answered*, not *answered no*.** C asked whether every-frame search is
necessary; the benchmark cannot distinguish "it is unnecessary" from "the interval is not in
control", because `searchInterval` is shadowed by `minimumContinuation` at the defaults. That the
thing a benchmark was meant to measure is not currently measurable is a legitimate result, and here
it is worth more than a number would have been.

The remedy is to make the relationship legible at the value rather than in a table:
`MatchSettings::searchIntervalShadowed()` says when `searchInterval < minimumContinuation`, which is
the same fix as `MotionCostBreakdown::caveat()` and the coverage report's inline reason — **meet the
reader at the number**.

**§28 was a units problem, not a tuning problem.** `switchMargin` was 0.05 in *raw cost units*
against a measured spread of 69.72 — four parts in ten thousand, which could never hold anything —
while the value that did hold (100) exceeds the entire spread and so holds indiscriminately. **No
default in raw units could have been right, because the sensible range depends on a scale nobody had
measured.** That is the fog bank's per-metre density defect in a new place.

It is now a **fraction of `MotionDatabaseStats::costSpread`**, measured at build time from the corpus
and the weights together, so it tracks both (ADR-389). The dial became readable immediately: 0.50
holds 6 of 150, 100.00 holds 147. **Changing the unit makes the default meaningful, not correct** —
what five percent *should* be still needs a motion-quality metric this phase does not have, and that
distinction is the point.

### And a bug the units work uncovered

`continueCost` — the cost of staying on the current motion, which the margin is compared against —
summed **raw squared deltas**, while `match.cost` applies per-dimension weights. §10 made those
weights live for the first time and nothing here was updated to match, so **the two sides of the
comparison were computed by different formulas.** At the default weight vector the discrepancy is
small, which is exactly why it survived; fixing it moved the 0.50 row from 3 held to 6.

**A comparison between two costs computed by different formulas is worse than no comparison: it has
a defensible-looking number on both sides.** This is the second defect §10's weighting change
created downstream — the first being the coverage resolution it made mutable — and both were found
by measuring something else.

## The feature-distance sweep — enumerated rather than tripped over

Two defects had already been found downstream of §10 making the weights live, **both by measuring
something else entirely**. Two accidents in a row is not a method, so the remaining sites were
enumerated: *every place that computes a distance, cost or comparison over a feature vector, and
whether it applies the weight vector.*

| site | was | now | intended? |
|---|---|---|---|
| `searchMotion` feature cost | weighted | weighted | yes — §10 |
| `MotionCostBreakdown` terms | weighted | weighted | yes — must equal the cost |
| `searchMotionStaged` coarse pass | weighted | weighted | yes |
| `searchMotionStaged` full pass | weighted | weighted | yes |
| `MatchMotionProvider::continueCost` | **unweighted** | weighted | **bug — §28** |
| `MotionDatabaseStats::costSpread` | **unweighted** | weighted | **bug — found here** |
| quality report nearest-neighbour | **unweighted** | weighted | **bug — found here** |
| the §16 test's own spread fixture | **unweighted** | weighted | **bug — found here** |

**Three more, and one was a scale I had introduced an hour earlier.** `costSpread` is the
denominator for §16's search severity and §28's switch margin, both of which compare *weighted*
costs — an unweighted denominator under a weighted numerator is the identical asymmetry the
hysteresis had. The quality report's nearest-neighbour distance is compared against
`duplicateRadius`, which is derived from the search's own discrimination and is therefore weighted.

**Both published numbers moved**: the cost spread from 69.72 to **52.57**, and the duplicate rate
from 57.02% to **61.68%**. Every severity figure in §16 was divided by the wrong scale.

Some of these could defensibly have been unweighted — a nearest-neighbour statistic describing the
corpus is arguably a property of the data. The rule is that **each must be unweighted on purpose and
say so**, not by having been written before the weights existed. Every one here is now weighted
because every one is compared against something weighted, and the comments say which.

### Why the category is dangerous rather than merely annoying

`costSpread` was written unweighted **by the person who had just finished diagnosing the identical
asymmetry in the hysteresis**, an hour earlier. That is not carelessness; it is evidence of how
strong the pull is:

> **A quantity's name does not carry the convention it was computed under, so the mismatch is
> invisible at the point of use and only visible at the point of definition.**

`costSpread` reads as obviously correct at every site that consumes it. Nothing about the identifier
says which space it lives in, and the weighted/unweighted distinction lives in the *call*, not in
the *name*.

**The structural fix is a type, and the comments are an interim measure.** A `WeightedCost` that
will not compare against a raw sum would have made all four of these bugs *unrepresentable* rather
than merely documented, and this codebase can express that. It is a larger change than the phase has
room for and is recorded here as the intended one, so the comments are understood as what they are.

### The general form

> **Making a dormant parameter live retroactively invalidates every consumer written while it was
> dormant.**

§10 did not introduce a bug into the hysteresis. It revealed that the hysteresis had been written
against a world in which weights did not exist — and the same for the spread, the nearest-neighbour
distance and a test fixture. This is a category the "mechanism present, measurement missing" table
does not cover, and it **predicts where the remaining instances are**: anything written *before*
§10 that touches feature-space distance. That is a finite, greppable set, which is why enumerating
it took minutes and finding the third by accident would have taken another night.

### And a rule the remedies keep converging on

`MotionCostBreakdown::caveat()`, the coverage report's inline reason, and
`MatchSettings::searchIntervalShadowed()` are the same fix three times:

> **A caveat belongs where the number is read, not where the number is explained.**

## §31 — RETRACTED: the +7.0 was an artefact, and the metric cannot measure this

**The first version of this section reported "+7.0 points, to perfect 100.0% leave-one-out
retrieval" as the largest before/after in Phase C. A control destroyed it.**

| | retrieval (n=158) |
|---|---|
| **before** — phase weight 0, as shipped | 76.6% |
| **after** — phase weighted 1.0, §13 fixed | 89.9% |
| **control** — phase values *shuffled* across samples | **97.5%** |

**The shuffle control did not merely fail to collapse — it beat the thing it controls for.** Random
phase retrieves *better* than real phase, and the reason is decisive: **random values are more
uniquely identifying than real ones.** Real phase is monotone within a clip and similar across
clips at the same point in a cycle; a shuffle gives every sample its own nonce.

**So leave-one-out retrieval is not a valid measure of phase-aware matching.** It rewards
*identifiability*, and identifiability is the opposite of what a matcher needs — a matcher exists to
find a **different** sample that is equivalent, not to find the one it was handed. Phase is very
close to a primary key within a clip, so adding it to the vector turns a match into a lookup.

**The original figure was an artefact twice over.** The perturbation stepped `d += 5` and the phase
dimensions sit at 33 and 34 of 35 — neither a multiple of five — so **the query carried the
held-out sample's exact phase**. And even perturbed, the metric could not have distinguished a
feature from an index. Perturbing every dimension also drops the *baseline* from 93.0% to 76.6%,
so the earlier absolute numbers were inflated as well.

**What this does and does not overturn.** §13's fix is still correct and still necessary — `Cyclic`
really was reaching the database as 0% and really is 95.4% now. What is retracted is the claim that
the fix buys measurable matching quality, because **nothing here has yet measured matching
quality.** The honest state of §31 is that phase-aware matching remains unevaluated, and the
instrument to evaluate it has to be **cross-clip**: does a query from clip A find the right *moment*
in a different clip of the same gait? Retrieval within a clip is the easy case and the one phase
trivially solves.

**This was the sixth perfect score to be fake tonight**, and the only one I had a motive to believe
— it was flattering, and a supervisor's prediction agreed with it in advance. That combination is
the condition under which a number goes unchecked, and it is why the control was demanded rather
than offered.

### And a caution that now extends backwards

§24's horizon experiment uses the same leave-one-out retrieval metric. Trajectory horizons are not
near-unique per sample the way phase is, so the effect should be far weaker — but **§24's numbers
were taken with the same `d += 5` perturbation**, and its conclusion (more horizons buy no
measurable gain) survives only if that holds under full perturbation. Flagged rather than assumed.


## §24 — RE-TAKEN under full perturbation, and the conclusion moved

The published table used the same `d += 5` perturbation §31 was retracted for, and here it was not
merely partial but **confounded**. The arms have **21, 25, 33, 37 and 41** dimensions, so the stride
left *a different subset of each arm's trajectory block* unperturbed — in the `{0.2}` arm the
trajectory dimensions are 21–24 and the last perturbed index below them is 20, so **that arm's
entire trajectory block carried exact values.** The flaw landed unevenly on exactly the variable
under test.

| horizons | dim | published | **corrected (full perturbation)** |
|---|---|---|---|
| none | 21 | 83.5% | **69.6% ±3.7** |
| {0.2} | 25 | 88.0% | **77.2% ±3.3** |
| **{0.2, 0.4, 0.6}** shipping | 33 | **93.0%** | **76.6% ±3.4** |
| {0.1, 0.2, 0.4, 0.8} | 37 | 92.4% | **81.0% ±3.1** ← best |
| {0.2, 0.4, 0.6, 0.8, 1.0} | 41 | 91.1% | **78.5% ±3.3** |

**Both halves of the old conclusion are gone.** The shipping default is no longer the best arm, and
"more horizons make it worse" is false — the 37-dimension arm leads. But the margins are inside the
error bars: 81.0 ±3.1 against 76.6 ±3.4 is 4.4 points on a combined error of ~4.6, so **no horizon
set is distinguishable from another at n=158.**

What survives, weakened: **trajectory features probably help** — 69.6 against 81.0 is ~2.4σ, no
longer the 3σ the partial perturbation showed, and only 1.4σ against the shipping default. §24's
honest state is **unresolved at this sample size**, which is a §20 question.

## §19 — checked, and it survives

§19's 100.0%-on-clip was the seventh ceiling and used the same `d += 5`. Re-taken with every
dimension perturbed: **still 100.0% of steps on the clip, and still a new sample on 1.00 of steps.**
It is real. The difference from §31 is structural rather than lucky — §19 asks whether the matcher
stays within the *right clip*, a question with ~66 acceptable answers per clip rather than one, so
an unperturbed dimension cannot convert it into a lookup the way it can a single-target retrieval.

## The rule that separates what survives from what does not

> **A measurement artefact is harmless exactly when it is constant across the thing being varied.**

- **§16's recall** compares two search plans over the same queries at the same dimensionality. The
  flaw hit both arms identically — **survives**.
- **§24's horizons** compares arms of *different* dimensionality, so the flaw landed differently in
  each — **does not survive**, and re-running moved the answer.
- **§31's retrieval** was an absolute quality claim with no comparison to shelter it — **did not
  survive at all**.

This is cheaper than re-running everything: it says in advance which numbers need re-taking. Ask
what varies between arms, and whether the flaw varies with it.

## On the conditions, not the standards

Six perfect scores were fake tonight and I caught five unprompted. The sixth arrived flattering, at
a ceiling, with a supervisor's prediction in front of it — and I banked it. Same standards, same
evidence-handling, different outcome.

**Rigour is not a property you have; it is a property of the conditions you are working under.** The
five that were caught had nothing riding on them. That is the argument for controls being
**mandatory rather than discretionary**: the moments you most need one are exactly the moments you
will feel least need of one.

And the shuffle control did something better than failing. **A control that merely fails says the
feature is inert. This one beat the thing it controls for**, which says the *metric* is measuring
the wrong quantity — and that invalidates every use of leave-one-out retrieval as a quality measure,
not one result.


## §24 — VOID, and removed from the §20 queue

The shuffle control was carried forward to the next use of the metric, as it should have been at
once. It returns §31's verdict and more strongly:

| arm | real | **shuffled** |
|---|---|---|
| no trajectory (baseline) | 69.6% | — |
| shipping `{0.2, 0.4, 0.6}` | 76.6% | **99.4%** |
| `{0.1, 0.2, 0.4, 0.8}` | 81.0% | **100.0%** |

Permuting the trajectory values destroys their meaning and keeps their distribution, so a feature
carrying meaning would collapse toward the 69.6% baseline. **It rises to a ceiling instead**, beating
the real features by 23 and 19 points — random values are more uniquely identifying than real ones,
exactly as phase was.

**So §24's corrected table measures nothing either, and the correct filing is not "unresolved at
this sample size".** A larger corpus would give tighter error bars around a meaningless number.
**§24 is not a §20 question**, and putting it on that queue would have implied a licensing decision
buys something it cannot.

**The scope is wider than either feature.** This is the second family the metric has failed on,
which is evidence it fails on any of them: leave-one-out retrieval admits **exactly one correct
answer per query**, so it rewards whatever identifies that answer, and every added dimension helps.
The metric is retired as a quality measure, not adjusted.

### The n question, answered — and why it is moot

n = 158 was a **choice**, not a limit: the loop strides `s += 11u` over 1,738 samples, and
leave-one-out can run on every one. n = 1738 would cut the standard error ~3.3×, turning ±3.4 into
about ±1.0 and the 4.4-point gap into ~3σ.

**And it would have been the more expensive mistake.** Tightening the bars first would have produced
a confidently wrong answer — 3σ of precision around a quantity the instrument does not measure.
**Validity first, then precision**, and validity failed, so the precision work is not worth doing on
this instrument at all.

### Which metrics are exposed, stated in advance

> **A metric is vulnerable to identifiability in proportion to how few correct answers it admits.**

- **Leave-one-out retrieval** — exactly one correct answer → maximally exposed → **retired**.
- **§19's stay-on-clip** — ~66 acceptable answers per clip → not exposed → **survives** (and it did,
  under full perturbation).
- **§16's recall** — one correct answer, but compared between plans at equal dimensionality, so the
  exposure is constant across arms → the *comparison* survives even though the absolute rate would
  not.

That ordering was available before any of these were run, and it is the cheap test for the rest of
the phase: count the acceptable answers before trusting the number.

## A valid instrument: cross-clip matching judged in pose space

Leave-one-out retrieval is retired. Its replacement had a trap in it that would have wasted the
build, and it is worth recording because it would have **looked like success at every stage**:

**Cross-clip retrieval needs a definition of "the right moment in another clip", and the obvious one
is the phase-aligned moment — which is circular.** It would score the matcher on recovering a target
*defined by the feature under test*; phase would win by construction, win harder the more weight it
got, and **the shuffle control would not catch it** — a shuffled phase cannot recover a
phase-defined target, so the control would pass and the result would still be worthless.

So the ground truth is **pose-space agreement**: the correct answers are the samples whose
model-space joint positions are closest. §23 had already established that the feature vector
deliberately contains no pose, which is exactly what makes pose available as an independent arbiter
— and it is what a matcher is ultimately for.

**Three guards are built in rather than run afterwards:**

1. **Many correct answers by construction.** Any sample within a margin of the best achievable
   cross-clip pose counts, so the question is "did it find an equivalent moment", not "did it find
   *the* moment". The margin is **derived**: the median pose distance between consecutive frames of
   one clip — two frames 1/30 s apart are interchangeable for matching — which is **0.0565 m** of
   mean joint offset. A property of the content, not of the matcher.
2. **The shuffle control is a test**, asserted, so the next person to add a feature gets the
   validity check without having to think of it.
3. **A degeneracy guard** reporting which clips the answers come from, since a matcher always
   picking the same clip-relative position would score well on similar-length clips without
   matching anything.

**And it validates:**

| arm | within margin (n=145) |
|---|---|
| phase weight 0 (baseline) | 32.4% |
| phase weighted 1.0 | **35.9%** |
| **phase SHUFFLED (control)** | **33.1%** |

**The control collapses back toward baseline**, which is the first of the three possible outcomes
and the one that makes the instrument usable: destroying the feature's meaning costs it almost
everything it gained. Real phase carries **+3.5** points; shuffled carries **+0.7**. Contrast the
retired metric, where shuffled phase *beat* real phase by 7.6 points and shuffled trajectory beat
real by 23.

**It is not yet a result, and the ordering says why.** At n=145 the standard error on a 35% rate is
~4.0 points, so +3.5 is inside the noise. **Validity is established, so precision is now worth
buying** — and n is a choice here as it was in §24 (the probe loop strides twice). That is the
correct order: it would have been wrong to tighten the bars first, and it is right to tighten them
now.

### The oracle, and then the answer

**The oracle rate is 100% by construction**, which is a correction to how the rates read. The
criterion is `chosenPose <= bestPose + margin` — within the margin **of the best achievable
cross-clip answer**, not within the margin absolutely — so the best available always satisfies it
and the metric never asks for an answer better than the corpus contains. It cannot be measuring
coverage.

What does need reporting is **how good the best available answer is**: median **0.1578 m**, against
a margin of 0.0565 m. So the margin is **0.36×** the typical best-answer distance — a demanding but
not absurd band, and the best cross-clip pose is typically **2.8× further away than two adjacent
frames of one clip**. That last figure is a real statement about this corpus's cross-clip coverage,
and it is a §20 quantity.

**The free control passes**: best-achievable is **identical across arms** (0.1578 m with phase off
and on), so the ground truth is not leaking from the feature vector.

**Then n, in the right order — and raising it dissolved the effect rather than tightening it:**

| | baseline | phase | shuffled | effect |
|---|---|---|---|---|
| n = 145 | 32.4% | 35.9% | 33.1% | +3.5 (±4.0 — noise) |
| **n = 435** | **34.7%** | **34.9%** | **32.0%** | **+0.2 (±2.3 — zero)** |

**Phase-aware matching shows no measurable benefit on this corpus.** The +3.5 was inside its own
error bar and said so; this is what honouring that warning looks like instead of explaining it away.
A +0.2 effect would need roughly n = 100,000 to resolve, which this corpus cannot supply at any
stride, so the honest statement is **no effect detectable here** rather than "not yet significant".

§13's fix remains correct and necessary — `Cyclic` really was 0% and really is 95.4%. What it does
not do is buy measurable matching quality, which is what the retraction suspected and this now shows
with an instrument that can tell meaning from identity.

Shuffled scoring *below* baseline (32.0 against 34.7) is the expected sign: dimensions carrying
noise cost a little.

### A limit on the shuffle control itself

> **A shuffle control validates a feature against a metric. It cannot validate the metric against
> the question.**

Nothing in a shuffle can tell you the ground truth was drawn from the wrong place — a phase-defined
target would have made shuffled phase collapse *correctly* while the whole result stayed worthless.
Two independent things have to be right, and only one of them has an automated check.


### Amendment: the third face resolved to "no change needed", and that is the better version

I recorded `phaseWeight = 0` as the third face of the night's category — *a default that was right
under the old behaviour and is wrong under the new one*. **The measurement has come back and the
default is not wrong.** With the phase data live and the weight on, there is no detectable benefit on
this corpus, so **`phaseWeight = 0` remains the correct value.** It stopped being correct for its
original reason and is now correct for a measured one.

**The category keeps its place and the hazard is unchanged:** *fixing a dead input does not fix the
configuration that was tuned around it being dead, and the fix has no way to find its own
dependents.* What changes is the instance: it resolved to **no change needed**, and the only way to
learn that was to measure rather than to assume the config had gone stale.

That is the stronger form of the lesson. **The hazard is that nobody checks — not that the value is
necessarily wrong.** And my first instinct, not to move the default until the cost side had been
measured, turned out right for a reason I did not have at the time.

A category whose every instance happens to be a bug is a category that has been curated. This one
has an instance that resolved to no-change, which is why both the finding and its resolution stay in
the record.

### Why the negative is strong rather than merely null

At a 34.7% baseline the matcher picks something notably worse than the best available **roughly two
thirds of the time** — so there was **ample headroom for phase to help, and it did not.** This is
not "no room to improve"; it is "plenty of room, and this feature took none of it". A ceiling effect
would have been the available get-out and there isn't one.

## §30 — and the first valid measurement of what a dimension costs

`dimension()` adds one contact flag per **feature** joint rather than per **contact** joint, so with
contacts enabled `head.x` carries one. A head does not plant: that dimension is **meaningless by
construction**, for a reason statable in advance — and **it varies, so it passes the liveness
check.**

That is the limit of that check, and it completes a set:

> **Liveness distinguishes present from absent. The shuffle distinguishes meaning from identity.
> Neither distinguishes meaning from noise.**

Each needs its own experiment, and the third one was sitting in the live system for free.

**The measurement.** Neutralising the bogus flag — setting it constant across samples, so it adds
the same amount to every distance and cannot affect any ranking — isolates the noise from the
dimension count, which is cleaner than deleting it. The oracle is asserted unchanged between arms,
since neutralising a feature cannot alter which pose-space answers exist.

| | within margin (n=435) |
|---|---|
| contacts, bogus head flag **live** | 33.3% |
| contacts, bogus head flag **neutral** | 33.3% |

**One meaningless dimension of 36 costs 0.0 points — nothing measurable.**

**That single point was low-powered by construction, and the dose-response reversed it.** One
dimension of 36 is a small perturbation measured with a bar wide enough to hide the effect: 0.0 ±
2.3 cannot distinguish "free" from "cheap". The claim under test is about a **slope**, so it needs
more than one dose.

Appending K fresh random dimensions — fixed at build, at **uniform weights** so that lengthening the
vector cannot silently change how the existing dimensions are treated:

| K | dimensions | rate | vs K=0 | **per dimension** |
|---|---|---|---|---|
| 0 | 33 | 32.2% | — | — |
| 1 | 34 | 32.4% | +0.2 | +0.23 |
| 4 | 37 | 31.0% | −1.1 | **−0.29** |
| 16 | 49 | 27.4% | −4.8 | **−0.30** |

**"Dimensions are not free" is true after all, and now it has a number: about −0.30 points per
useless dimension.** K=4 and K=16 agree to within 0.01, which is a slope rather than noise, and the
K=1 point is simply below the resolution of a single measurement — exactly the low-power failure the
dose-response was run to escape.

**So the earlier "not supported" verdict was wrong, and it was wrong in the direction of my own
convenience** — it let a claim I had been repeating be quietly retired rather than measured
properly. The correction: the claim stands, with a magnitude, and the single-point null should have
been reported as *underpowered* rather than as evidence of absence.

It also reconciles with the shuffle. Two shuffled phase dimensions cost 2.7 points — about 1.35 each
— against 0.30 for an appended one. Shuffling **destroys a signal and adds noise in its place**;
appending only adds noise. The larger figure for the destructive operation is what should be
expected, and the two are consistent rather than corroborating.

It does not contradict the shuffle result. Neutralising removes a dimension's contribution;
shuffling **replaces it with active noise**, which cost 2.7 points for two dimensions. Removing a
weak signal and injecting a strong one are different operations and the asymmetry is expected.

**−0.30 is a unit-weight figure and must be cited with that condition attached.** What was measured
is *the cost of one extra unit-weight dimension in a vector where every dimension carries unit
weight* — because the dose-response had to run at uniform weights to keep the arms comparable.

**So the obvious use of it is wrong, and I had proposed it.** I suggested subtracting ~2.4 points
from §24's five-horizon arm for its 8 extra dimensions. That subtraction assumes the extra
*trajectory* dimensions contribute to the distance the way the synthetic unit-weight ones did, and
under the production weight vector they do not — a dimension's cost scales with how much it
contributes. **Importing the constant into a weighted run is the `costSpread` asymmetry again: a
quantity whose name does not carry the convention it was computed under.**

The clean fix is the one that keeps recurring: **make the conditions constant across the
comparison.** Re-run §24 at uniform weights too, so the correction is measured and applied under one
regime. A weighted slope is obtainable but needs the padding trap solved first — by extending the
weight vector explicitly rather than letting it fall back to unweighted.

**What it changes:** the cost side of every dimension decision now has a real number instead of a
slogan, for unit-weight vectors.

**But the reasoning published for three decisions was borrowed from an untested claim, and that is
worth marking even where the conclusion survives**, in the same way the `Travelling` comment was
corrected rather than quietly replaced:

- **the horizons** — the cost side was argued on "dimensions are not free" before it was measured;
  the conclusion is void anyway, because §24's instrument was retired.
- **the phase weight** — declining to move it was justified *independently* once the effect measured
  zero, so the decision stands; the published reasoning leaned on the untested claim.
- **the contact flags** — the same, and the flag is wrong for a reason that needs no measurement.

The distinction most likely to be lost is the one that matters: **the earlier reasoning was wrong
even where the conclusion survives.**

The flag itself is wrong regardless of the measurement — a contact flag on a head is not a judgement
call — and is fixed separately, after the experiment rather than before it.

## A standing practice, not a note about one case

> **Measure the cost before moving a shipping default.**

Declining to move `phaseWeight` until the cost side had been measured was the rule that survived the
number turning out to be an artefact. The large, significant-looking +7.0 was retracted; the refusal
to act on it still stands. **A rule that protects you when your evidence is wrong is doing more work
than one that protects you when it is right**, which is the argument for this being standing practice
rather than a remark about that instance.


### Two pre-tests now, both available before the result

> **1. Count the acceptable answers before trusting the number.** A metric is vulnerable to
> identifiability in proportion to how few correct answers it admits.
>
> **2. A single-point null is underpowered, not evidence of absence.** One point near the origin
> cannot distinguish a slope from a flat line, and the tell is available *before* the result.

Both were available in advance of the failures they would have caught, which is what makes them
pre-tests rather than post-mortems.

# THE THIRD INSTANCE: a statement about the encoding, not about a third feature

Three features intended to describe periodic gait have now been measured on the validated
cross-clip instrument, each with its own shuffle control:

| feature | baseline | real | shuffled | verdict |
|---|---|---|---|---|
| **phase** (2 dims) | 34.7% | 34.9% | 32.0% | no detectable benefit |
| **trajectory** (on the retired metric) | 69.6% | 76.6% | **99.4%** | shuffled beat real |
| **contacts** (2 dims) | 34.7% | **33.3%** | **34.9%** | **worse than baseline, and shuffled beat real** |

**Contacts are net −2.0 points**: −1.4 against baseline, plus a **0.6-point dimensional hurdle** (two
dimensions at the measured −0.30 each, unit weight). This is the first feature in the phase judged
on *net value* rather than on whether it helps, and the answer is that it is worth less than the
dimensions it occupies.

**Three independent features behaving this way under one encoding is a statement about the encoding
rather than three separate feature failures.** Folding it into §30 would lose that.

## The mechanism, stated as a hypothesis with its test

The candidate explanation is **redundancy, not absence of signal.** The pose block already contains
foot and head positions *and velocities*. A foot's position and velocity jointly encode where in the
gait cycle the body is and whether that foot is planted — so phase and contact state are **already
present implicitly**, and the explicit features re-state them.

That predicts exactly what is observed, including the part that looks paradoxical:

- Adding a redundant feature costs its dimensions and buys nothing → phase, at +0.2.
- A redundant feature **double-counts a signal already present**, over-weighting gait phase relative
  to everything else in the distance → contacts, at −1.4, *below* baseline.
- **Shuffling a redundant feature removes the double-counting** while paying only the noise cost →
  shuffled beats real, which is otherwise hard to explain.

**This is a hypothesis and it is falsifiable now, on the corpus in hand:** measure the correlation
between the explicit gait dimensions (phase, contact) and the pose dimensions. High correlation
supports redundancy; low correlation kills it and sends the search elsewhere. That measurement is
the next thing to do and it does not need §20.

**What it would mean if it holds:** the feature vector is over-specified for this corpus, and the
right response is not to tune the weights of the explicit gait features but to **remove them** — the
opposite of what §30 and §31 were written to add. That would change what the feature vector should
be rather than what its weights should be, which is why it is recorded above the section level.

## And the flag is fixed

`MotionFeatureConfig` now carries `contactJoints` separately from `joints`, because they answer
different questions: `joints` are the ones whose position and velocity describe the pose, and a head
is a good pose feature and a meaningless contact. Empty falls back to `joints`, which is the old
behaviour. `defaultBipedConfig` names the two feet. Contact dimensions: **2 of 35**, down from 3 of
36.

The natural experiment that bug provided — neutralising the bogus flag — is recorded in the test
file rather than deleted silently: it measured 0.0 points, **that null was underpowered, and the
dose-response reversed it.**
