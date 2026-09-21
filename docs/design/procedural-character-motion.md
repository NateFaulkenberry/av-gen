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
