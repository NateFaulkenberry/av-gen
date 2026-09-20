# Autonomous character animation and motion synthesis: research and architecture

**Status:** Phase 0 research. **Nothing here is implemented.** This is a proposal for review.
**Branch:** `agent/anim-research`. **ADR range:** 540-559.
**Date:** 2026-09-20. **Machine for every measurement below:** Apple M2 Max, 8P+4E, 64 GB.

Every claim about AV Gen in this document cites a file and a line that was read, and every number
is either a measurement with its probe named or is labelled an estimate (ADR-385). Two claims I
made early were wrong and are recorded as such in §1.9, because the shape of the mistake is worth
more than the tidy version.

---

## 0. The three sentences

1. **AV Gen's animation engine is much further along than the brief assumes**, and the parts it is
   missing are not the parts the brief expects. It has a masked layer stack, analytic two-bone IK,
   foot planting on a ground plane, root-motion extraction with a transfer-of-authority contract, a
   utility-AI decision layer with considerers and hysteresis, a budgeted perception system, a nav
   grid, an action queue with authority tiers, and pose-rate LOD. What it does not have is a way to
   get a second file's animation onto a rig, a blend space, phase-matched transitions, or a leg
   the IK solver can see.

2. **Motion matching cannot be built on the content that exists.** The entire Glowmere alien motion
   corpus is **57.07 seconds** across 26 clips, and **every locomotion clip is authored in place** —
   `Walking`'s root travels at most 3.2 cm in X and 2.7 cm in Z across its 1.03 s cycle (§1.8).
   Motion matching's central feature is the *future root trajectory*, and there is no root
   trajectory in this data to extract. The first blocker is not the algorithm; it is that AV Gen
   cannot load a motion dataset at all (§1.5).

3. **What to build first is a motion-data foundation and two small quality fixes that pay for
   themselves immediately** — cross-file clip binding + retargeting, phase-matched transitions, and
   a leg the foot solver can actually solve. The smallest experiment that would validate or kill
   the whole architecture is in §16 and takes one throwaway tool, no engine changes.

---

## 1. What AV Gen already has

### 1.1 Inventory

| | what exists | where |
|---|---|---|
| **Skeleton** | `Joint{name, parent, rest}`, topologically ordered, parents before children; palette + inverse binds; `kMaxPaletteJoints = 256`; joint masks with weights, `descendants`, and a `missing` report | `src/scene/skeleton.hpp:39-140` |
| **Pose** | `Pose{std::vector<Transform> local}` — local TRS only; model space derived, never stored | `src/scene/skeleton.hpp:60-65` |
| **Clips** | `AnimationClip{name, start, duration, channels}`; `AnimationChannel{joint, path, interpolation, times, values}`; glTF Linear/Step/CubicSpline; `start` is the first key, not zero | `src/scene/animation.hpp:42-69` |
| **Sampling** | `sampleClip` writes only the channels the clip carries, over a seeded rest pose | `src/scene/animation.hpp:83` |
| **Blending** | `blendTransform` (lerp/lerp/shortest-arc slerp), `blendPose` | `src/scene/skeleton.hpp:134-138` |
| **State machine** | `AnimationPlayer`: named states over clips, wildcard transitions, **exactly two slots** and one cross-fade | `src/scene/animation.hpp:105-198` |
| **Layers** | `PoseLayerStack`: ordered masked layers, kinds `Aim` / `Additive` / `Foot`, drives `Manual` / `Look` / `Reaction` / `Ground`, per-layer `LayerResolution` telling apart eight ways of doing nothing | `src/scene/pose_layers.hpp:44-310` |
| **IK** | Analytic two-bone (`solveTwoBone`), closed form, no seed, `IkStatus` distinguishes Solved / Clamped / DegenerateBone / DegenerateTarget / DegenerateBend | `src/scene/ik.hpp:39-129` |
| **Contact** | `plantOnPlane`: drop the tip onto a ground plane along model-space −Y, preserving the rest-pose standing height; `footAlign` lays the sole flat; `soleUp` resolved from the rest pose | `src/scene/pose_layers.hpp:128-170`, `:296-305` |
| **Root motion** | `RootMotionSet` per clip, opt-in, axis-maskable; read from the lowest-indexed translated joint, **compensated at the topmost ancestor**; a pure function of (clip, joint, axes, second) | `src/scene/root_motion.hpp` (whole file) |
| **Rig LOD** | `updateHz` on a fixed timeline grid `floor(t*hz)/hz`; `nearDistance` / `farHz` / `cullDistance`; `RigStats` counts posed / rateLimited / culled / joints / layers | `src/scene/animation.hpp:213-232`, `:305-323` |
| **Motion blur correctness** | `previousPalette` + `reseedPrevious` so a scrub does not smear 71.5 model units of joint motion nobody made | `src/scene/animation.hpp:262-278` |
| **Behaviour seam** | `entity::LocomotionState` — activity, timeline second, position, yaw, speed, turn rate, grounded, reaction, look target, ground plane, action name, playback rate, blend | `src/entity/locomotion.hpp:52-133` |
| **Gait** | Hysteresis (separate enter/exit bands) + minimum dwell + asymmetric accel/decel + playback-rate matching + a `footSlip` diagnostic | `src/entity/gait.hpp:29-127` |
| **Decision** | `Selector` + `IConsiderer`: scored options, dwell in *ticks* not seconds, margin, refusal counters, `decisionDebug` exposing losing scores | ADR-333; `src/entity/decision.hpp` |
| **Actions** | `ActionKind{Wait, Move, Face, Pose, Interact, Equip, Unequip, Set}` with conditions, branch labels, resumption, results-with-reasons, and three authority tiers | `src/entity/action.hpp:197-257` |
| **Debug draw** | 25+ overlay switches including `skeletons`; per-lab overlay profiles | `src/rendering/debug_view_options.hpp`, `src/labs/overlays.hpp` |
| **Offline host** | A general job system with staged progress, honest ETA, prompt cancellation, worker threads | `src/app/job_system.hpp` |

### 1.2 The seam, exactly

```
behaviour → LocomotionState → Composition::AnimationSink::setLocomotion   (composition.cpp:2392)
                              ├─ driveLayers(state)                        (composition.cpp:2433)
                              │    Look     → layer.weight/target (world→entity-local via the
                              │               node's WORLD transform, incl. the 1.94× scale)
                              │    Reaction → layer.weight
                              │    Ground   → layer.groundPoint/Normal, normal transformed by
                              │               transpose(mat3(world)) because a normal is a covector
                              └─ entity_.clipFor(activity|action) → setNodeAnimation(node, want,
                                    state.time, state.blend, state.playbackRate)
                                                                           (composition.cpp:2409)

animation → IRootMotionSource::rootMotion(now) → entity adds to EntityState::travel
                                                                           (composition.cpp:2527)
```

Two properties of this seam are worth protecting and are easy to break:

* **A behaviour never names a clip.** It names an *activity*; `EntityDesc::clips` turns that into
  whatever the asset shipped (`composition.cpp:2400-2402`). This is what lets one routine drive an
  alien, a bull and a chicken.
* **A pose layer structurally cannot move the body.** `pose_layers.hpp` includes nothing from
  `entity/` and nothing from the composition, so `MotionAuthority::Simulation` is unreachable from
  it by construction rather than by policy (`src/scene/pose_layers.hpp:12-18`). Root motion runs
  the other way and is the only exception, and it is asked for by name.

### 1.3 Determinism, as actually contracted

ADR-360's contract is *"a render must still be reproducible; scrub may differ from play; two
renders of the same range may not differ from each other."* Against that:

* `AnimationPlayer` stores *when* a state was entered, not how long it has run
  (`src/scene/animation.hpp:8-16`), so the pose is a pure function of (states, entry times, blend,
  now).
* `PoseLayerStack::apply` integrates nothing and remembers nothing across frames
  (`src/scene/pose_layers.hpp:21-27`).
* `solveTwoBone` is closed-form and has no seed (`src/scene/ik.hpp:6-13`) — deliberately, because
  an iterative solver seeded from last frame is exactly the state a scrub cannot reproduce.
* `EntityWorld::seek` integrates a **fixed** step (`src/entity/entity.cpp:919-940`, and the comment
  at `:996`), while play integrates `time.deltaTime` (`src/scene/composition.cpp:2727`, `:2741`).
  That divergence is *permitted* by ADR-360 and is not a defect. ADR-267's measurements
  (`docs/character-ai-research.md` §G.1) put play-vs-seek at 0.000022 m at 30 s and jittered
  play-vs-seek at 0.094877 m.

**Everything proposed below must land on the pure-function side of that line, or be in the entity
simulation, which is replayed.** A motion-matching search that remembers "the frame I was on" is
state, and it belongs in the entity tier, not in `scene/`.

### 1.4 Threading

There is none in the animation or entity path. `grep -rn "std::thread\|jthread\|std::async" src/scene src/entity` returns nothing. A worker pool exists (`src/app/job_system.hpp`) and is used by
analysis, ecology, terrain, path tracing and render jobs. `RigStats::cpuMs` is measured
(`src/scene/animation.cpp:552`) and **read by no one** — `grep -rn "cpuMs" src/ | grep -i rig` is
empty. That is a free instrumentation point already paid for.

### 1.5 The blocker nobody has hit yet: there is no way to get animation onto a rig

`Importer::importClips` walks `rigForSkin_` — the rigs built *in this same `loadGltf` call* — and
pushes each clip onto them (`src/assets/gltf_loader.cpp:342-364`). There is no API anywhere that
binds a clip from file A to a skeleton from file B. The asset attribution file states the
consequence plainly: each of the six alien variants carries its own copy of all 26 animations,
**3.9 MB of 4.5 MB per file**, "because AV Gen has no way to bind one file's clips to another
file's skeleton" (`assets/aliens/ATTRIBUTION.md`).

There is also **no retargeting of any kind** in the codebase: no joint-name mapping table, no
skeleton normalisation, no bind-pose reconciliation, no bone-length scaling. Every clip in the
repository was authored on the exact skeleton it is played on.

This is the first thing any motion-database work needs and it does not exist. It is not a small
thing and §12 prices it.

### 1.6 What the animation player cannot do

* **Only two slots.** A state change mid-cross-fade drops the third pose outright
  (`src/scene/animation.cpp:245-254`). Three-way fades are refused on stated grounds, and those
  grounds are sound for a cross-fade — but they are the reason there is no blend space.
* **No phase matching.** `play()` sets `current_.start = now` (`src/scene/animation.cpp:251`), so
  `Walking → Running` cross-fades from an arbitrary walk phase into running *phase zero*. Whether
  the feet line up is luck. Nothing in the engine knows a clip has a phase.
* **No blend space.** `docs/character-ai-research.md:677` already names this as rejected: "the
  engine has one cross-fade between two slots. A blend space is three steps past the layer stack
  that does not exist yet." The layer stack now exists (ADR-300). The rejection is due a re-read.
* **No inertialization.** `grep -rni "inertial" src/ docs/` returns nothing. Every transition is a
  cross-fade, which means the cost of a transition scales with how many joints disagree.
* **Root motion is translation only.** `RootMotionSample` carries `glm::vec3 displacement` and no
  rotation (`src/scene/root_motion.hpp`). A turn-in-place clip's yaw cannot be handed to the
  simulation, which is why `Idle_turn` is played *while* the behaviour turns the body by its own
  arithmetic.

### 1.7 No animation authoring UI exists

`src/ui/` has 30 panels; none of them is an animation, character or rig panel. The entire
animation surface in the editor is **two debug toggles** — "no animation" and "freeze animation" —
in the control panel's diagnostics section (`src/ui/control_panel.cpp:2231-2236`). Pose layers, gait,
clips maps, considerers and actions are authored in scene JSON by hand. Every debugging affordance
is either a debug-draw switch, a lab fixture, or a test. This is a real constraint on anything
whose workflow assumes an artist tuning weights.

### 1.8 The measurement that decides the motion-matching question

Probe: `scratchpad/anim-research/clipstats.py` and `rootpath.py`, reading
`assets/aliens/alien-scout.glb` directly.

```
26 clips · 57.07 s of motion · 6,942 channels · 156,921 keys
1,712 frames at 30 fps · 3,424 at 60 fps
```

Every clip has **267 channels** — 89 joints × (translation, rotation, scale) — whatever moves. The
rig is 89 deform joints and is shallow: depth histogram `{1:18, 2:33, 3:17, 4:13, 5:8}`. Eighteen
joints hang directly off the armature. Every joint carries a baked translation channel in every
clip. This is a fully-baked rig, not a hierarchical one.

Root excursion over the whole clip, `root.x`, in the file's own metres:

| clip | X range | Y range | Z range | root path length |
|---|---|---|---|---|
| `Idle` | 0.0014 | 0.0052 | 0.0084 | 0.021 |
| `Walking` | 0.0322 | 0.0714 | 0.0212 | 0.303 |
| `Running` | 0.0341 | 0.0964 | 0.0534 | 0.460 |
| `Idle_turn` | 0.0213 | 0.0301 | 0.0185 | 0.128 |
| `Jumping` | 0.1613 | 0.8725 | 0.1076 | 2.486 |

**Every locomotion clip is in place.** The only clips with net displacement are the four dying
animations and `Landing` (−0.567 m vertical), which ADR-260 already inventoried.

The consequence is not "motion matching would be low quality". It is that **the inputs do not
exist**. Motion matching queries a database on a future root trajectory — typically root position
and facing at +0.33 s, +0.66 s and +1.0 s in character space. You compute those from a root that
travels. This root does not travel. Nor is there any transition content: no starts, no stops, no
planted turns, no strafes, no direction changes, one walk cycle and one run cycle.

For comparison, shipped motion-matching databases are measured in tens of minutes to hours of
mocap. AV Gen has 57 seconds, and about 20 of those seconds are locomotion.

### 1.9 Two things I asserted and had to retract

Recorded because the shape matters more than the conclusions.

* I concluded from `node["layers"] == null` on all five aliens that **look-at and reaction layers
  were authored on nobody in the production scene**. They are authored under
  `node["animation"]["layers"]`, and all five aliens carry a `look` aim layer over
  `head.x`/`Eye_L`/`Eye_R`/`Mouth`/`Antenna` and a `startle` additive layer. I was reading the
  wrong key and the null was real.
* I nearly wrote that `RigStats::cpuMs` was a per-frame clock driving something. It reports and
  never drives (`src/scene/animation.hpp:320-322`), and it has no reader at all.

### 1.10 The one that survived: the alien has no leg the solver can see

Foot IK (ADR-359) is authored in exactly one scene — `examples/labs/footik/foot-ik-lab.scene.json`,
four `foot` layers on the farm animals. `grep -c '"kind": *"foot"'` over
`glowmere-valley-2-multicam.scene.json` and `glowmere-valley-2.scene.json` returns **0** for both.
No Glowmere alien has ever had a foot planted.

That is not an oversight. `PoseLayerStack::bind` requires `chainMid` to be a descendant of
`chainRoot` and `chainTip` a descendant of `chainMid` (`src/scene/pose_layers.hpp:151-156`), and on
this rig the left leg is spread across **three separate branches**:

```
 10 root.x            <- rig
  4 thigh_twist.l     <- root.x
  3 thigh_stretch.l   <- thigh_twist.l
  2 thigh_twist_2.l   <- thigh_stretch.l
 85 leg_stretch.l     <- rig              ← a SIBLING of root.x, not under the thigh
 83 leg_twist.l       <- leg_stretch.l
  1 foot.l            <- root.x           ← a direct child of root.x, not under the knee
  0 toes_01.l         <- foot.l
```

Hip, knee and foot are not an ancestor chain. Any three names an author reaches for resolve to
`LayerResolution::NoChain`. **Foot planting, terrain adaptation and step-over are structurally
impossible on the Glowmere alien today**, and no amount of motion synthesis fixes that — the
correction layer downstream of it cannot run.

ADR-359 saw this ("the alien pack is the reason this is a first-class answer rather than an
assertion") and scoped itself to the farm rigs. Nothing has changed since.

### 1.11 The character-intelligence layer, which is most of what Parts 12-13 ask for

`src/entity/` is **15,562 lines** and already implements the goal→behaviour→intent spine the brief
proposes building.

* **Twelve behaviours**, registered by name: `hover`, `liveliness`, `drift`, `bank`, `spin`,
  `wander`, `lookAt`, `interest`, `explore`, `ground`, `decide`, `orbit`
  (`src/entity/behaviors.cpp:2496-2537`).
* **Five considerers**: `idle`, `holdPost`, `investigate`, `interest`, `route`
  (`src/entity/decision.hpp:533-536`). `route` prices the *way* rather than the place (ADR-336),
  costing two A\* searches per destination and capping itself at four destinations because a search
  is 24.969 µs (`src/entity/decision.hpp:503-507`).
* **A\* with string-pulling** over a 4 m nav grid, tie-broken on cell index so the path is machine-
  independent (`src/entity/nav_grid.hpp:30-31`, `:308`), with flood-filled reachability regions, a
  `PathStatus` reason enum, dynamic `rebuildRect` for a door that closes, and a `vouches()`
  self-check that refuses to answer for a world it got wrong (`:263`).
* **Perception**: range 60 m, FOV 200°, capacity 8 percepts, 4 Hz cadence as a *pure function*
  `senseTick(time, hertz, seed)` with a seed-derived phase so a crowd does not all sense on the
  same frame (`src/entity/perception.hpp:83-93`). Line of sight is implemented and **off by
  default** because it costs 1800.6 µs at 20 m — 172 ms a frame at 100 characters
  (`src/entity/character_ai.hpp:142`).
* **Grounding**: `GroundFollower` takes up to 6 height queries + 1 normal query per body per step,
  smooths height on a 45 ms one-pole and slope on a 240 ms one-pole, leans 0.55 of the way to the
  normal clamped at 34°, and publishes `surfaceHeight`/`surfaceNormal` for foot IK
  (`src/entity/grounding.hpp:31-119`, `grounding.cpp:48-90`).
* **Terrain query cost, measured and documented in the code**: `WorldMap::height` **1.14 µs**
  (`nav_grid.hpp:225`); the full walkability ladder `Navigator::sample` **10.325 µs**
  (`character_ai.hpp:137`); grid bilinear `groundAt` **~0.01 µs**; `clearanceAt` 0.024 µs;
  `obstructed` 0.011 µs; `ObstacleField::segmentBlocked` 0.018 µs.
* **Entity LOD**: a cull band and a coarse band, the coarse band accumulating dt and running one
  step with the accumulated dt while still writing the transform on skipped frames
  (`src/entity/entity.cpp:1343-1387`). Cadence throttles (perception 4 Hz, decision 2 Hz) are
  phase-shifted pure functions.
* **Randomness is PCG32 keyed per entity from `seed` or FNV-1a(name, sceneSeed)**
  (`src/core/rng.hpp:3-4`, `src/entity/entity.cpp:45-55`, `:100-103`). `std::random_device` is
  banned and `std::mt19937` distributions are rejected as platform-dependent.

**What does not exist at this level:** a desired-velocity *vector*. Velocity is polar —
`EntityState{speed, yaw}` — and every behaviour integrates `travel` itself
(`src/entity/behavior.hpp:50-91`, and e.g. `src/entity/action.cpp:770-771`). Motion matching,
trajectory prediction and blend spaces all want a 2D velocity and a 2D facing as separate
quantities, and the engine's representation cannot express "moving north-east while facing north".
That is a small change with a wide blast radius and it is the first API decision in §11.

### 1.12 Stale comments that would mislead the next reader

Each is a claim in a comment that the code contradicts. Worth fixing as a by-product, and worth
knowing about before trusting any other comment in these files.

| claim | where | contradicted by |
|---|---|---|
| the navigator has "no graph and no search" | `src/entity/action.hpp:100-105` | `NavGrid::findPath`, A\* + string pull, `src/entity/nav_grid.hpp:308` |
| `reaction`/`lookTarget`/`hasLookTarget` are read by nothing | `src/entity/character_ai.hpp:348-353` | `src/scene/composition.cpp:2484-2491` |
| `ISkeletonQuery` has zero implementations; `setSkeleton` zero call sites | `src/entity/character_ai.hpp:361-363` | `src/scene/composition.cpp:2191` |
| nothing includes `character_ai.hpp` | `src/entity/character_ai.hpp:12-14` | `entity.hpp:34`, `decision.hpp:39`, `perception.hpp:36` |
| "Nothing reads `Entity::percepts()` yet" | `src/entity/entity.cpp:1446-1449` | `decide` / `investigate`, `src/entity/decision.hpp:316-318` |
| root motion "is not extracted" | `docs/character-animation-lab.md:39` | ADR-337, `src/scene/root_motion.hpp` |

### 1.13 What must not be touched

* `AnimationPlayer`'s "store *when*, not how long" rule and its two-slot cross-fade semantics. A
  motion-matching provider must be a **sibling** of the player, not a rewrite of it.
* `PoseLayerStack`'s structural inability to reach the simulation. Any new layer goes through the
  same door.
* `solveTwoBone`'s closed form and its refusal to guess a bend plane.
* `EntityWorld::seek`'s fixed-step replay and `IBehavior::historySteps` opt-out.
* `Rng` = PCG32 keyed on (seed, index). No new randomness source.
* The rule that no layer names a clip.

---

## 2. Classical motion matching, and whether AV Gen should build it

### 2.1 What it is, precisely

A motion database is every frame of every clip, resampled to a fixed rate. Each frame carries a
**feature vector**; Holden's canonical locomotion layout is R^27:

```
t_t ∈ R^6   future root positions at +20/+40/+60 frames, 2D, in character space
t_d ∈ R^6   future root facing directions at the same three times, 2D
f_t ∈ R^6   two foot positions, 3D, character space
ḟ_t ∈ R^6   two foot velocities
ḣ_t ∈ R^3   hip velocity
```

Each column is normalised by its dataset standard deviation, then multiplied by a user weight —
and production implementations fold the weight into the stored value at build time so the runtime
query is a plain L2 distance. Every N frames (N ≈ 10, so 6 Hz at 60 fps) the controller builds a
query from the user's desired trajectory plus the character's *current* foot/hip state, searches
for the nearest database row, and if the winner beats the frame it would otherwise have played by
a margin, jumps there and **inertializes** the discontinuity away.

### 2.2 The three things that make it work, none of which AV Gen has

1. **A root that travels.** §1.8. Without it there is no `t_t`, no `t_d`, and no `ḣ_t`.
2. **Transition content.** The quality of motion matching is the quality of the database's
   coverage of starts, stops, planted turns, direction changes and speed changes. AV Gen has one
   walk cycle and one run cycle.
3. **Inertialization.** `grep -rni "inertial"` over `src/` and `docs/` is empty. Motion matching
   without inertialization is a cross-fade every 10 frames, which is worse than what the engine
   does today.

### 2.3 What a search actually costs — measured

Probe: `scratchpad/anim-research/mmprobe.cpp`, standalone, linking nothing from AV Gen. It is
ADR-182-safe: a known-best row is planted in the database and the search must find it, in both
arms, before any timing runs; the planted row is then re-randomised so the timing is not an
early-out best case. Minima over 200 repeats (ADR-170), Apple M2 Max, `clang++ -std=c++20 -O2`.

| frames | D=27 (MB) | D=27 brute (µs) | D=51 (MB) | D=51 brute (µs) | ns/frame D=27 |
|---:|---:|---:|---:|---:|---:|
| 1,712 (*AV Gen's whole corpus*) | 0.18 | **11.5** | 0.33 | 27.8 | 6.72 |
| 5,000 | 0.51 | 33.0 | 0.97 | 81.1 | 6.60 |
| 20,000 (11 min @30) | 2.06 | **132.1** | 3.89 | 324.7 | 6.61 |
| 60,000 (33 min @30) | 6.18 | 405.5 | 11.67 | 977.2 | 6.76 |
| 180,000 | 18.54 | 1,217.5 | 35.02 | 2,938.2 | 6.76 |
| 540,000 | 55.62 | 3,640.1 | 105.06 | 8,833.2 | 6.74 |

For scale, a full clip sample + `poseToModel` + `jointPalette` for the 89-joint alien costs
**2.97 µs** (`scratchpad/anim-research/poseprobe.cpp`; a faithful reimplementation of the
arithmetic shape, *not* AV Gen's code — treat it as a lower bound). So a 20,000-frame search is
**44× the cost of posing the character it is for**, and a 60,000-frame search at D=51 is ~1 ms, or
0.16 ms/frame amortised at a 6 Hz search rate.

**The finding that surprised me, and is worth keeping:** the textbook early-out optimisation —
abandon a row the moment its running cost exceeds the best so far — is **2.0× to 2.7× slower** on
this machine at every size tested (e.g. 20,000 × D=27: 132.1 µs brute vs 357.5 µs early-out). The
branch in the inner loop defeats clang's autovectorisation, and on Apple Silicon the vectorised
straight-line loop wins by more than the early-out saves. Anyone implementing this should measure
before adding the "obvious" optimisation.

At AV Gen's actual scale these numbers say motion matching is cheap. They also say the cheapness is
irrelevant, because the database is 57 seconds.

### 2.4 Acceleration structures: don't, yet

Holden's own C++ implementation uses a two-level AABB BVH over groups of 16 and 64 frames. At
20,000 frames a brute-force search is 132 µs; a tree buys perhaps 5-10× and costs determinism risk
(tie-breaking depends on traversal order and on how the tree was built) and a build step. The
straight-line SIMD loop is the right default until the database passes ~100,000 frames, which AV
Gen will not reach for years.

### 2.5 Reference implementations, assessed

| repo | license | LOC | verdict |
|---|---|---|---|
| **[orangeduck/Motion-Matching](https://github.com/orangeduck/Motion-Matching)** | **MIT**, © 2021 Daniel Holden, real LICENSE file | 4,742 C++ (controller.cpp 2,486; database.h 732; lmm.h 257; **nnet.h 173**; spring.h 214) + 2,355 Python training | **The only artifact in this study that could be vendored.** Written by the paper's first author, last pushed 2025-02-06. Contains both classical MM and learned MM. raylib is the demo window only. |
| [SaxonRah/OpenMotion](https://github.com/SaxonRah/OpenMotion) | see §7 | see §7 | see §7 |
| [aaron1a12/wm-motion-matcher](https://github.com/aaron1a12/wm-motion-matcher) | see §7 | | |
| [KamatMayur/UE5_MotionMatching](https://github.com/KamatMayur/UE5_MotionMatching) | see §7 | | |

**The data is the catch, not the code.** Holden's repo builds its database from Ubisoft **LAFAN1**,
which its own README says is **CC BY-NC-ND 4.0** — "unlike the code, which is licensed under MIT".
So the algorithm is free and the reference motion is not.

---

## 3. Learned motion matching

### 3.1 The architecture

Three runtime networks replace the three stages of classical MM (Holden, Kanoun, Perepichka, Popa,
*Learned Motion Matching*, ACM TOG 39(4), SIGGRAPH 2020,
<https://doi.org/10.1145/3386569.3392440>). A fourth, the Compressor, exists only during training.

| net | replaces | input | output | layers | width | activation |
|---|---|---|---|---|---|---|
| Compressor | — (training only) | pose `y` + its character-space FK | latent `z ∈ R^32` | 5 | 512 | ELU |
| **Decompressor** | the animation database `Y` | `[x ; z]` | pose `y` | 3 | 512 | ReLU |
| **Stepper** | stepping through the database | `[x_i ; z_i]` | deltas `δx, δz` | 4 | 512 | ReLU |
| **Projector** | the nearest-neighbour search | noisy query `x̂` | `x_{k*}, z_{k*}` | 6 | 512 | ReLU |

Published results (Table 3), single-threaded on an Intel Xeon 3.5 GHz with a hand-written inference
library:

| scenario | frames | MM memory | LMM memory | reduction | MM µs/frame | LMM µs/frame |
|---|---:|---:|---:|---:|---:|---:|
| Locomotion | 89,480 | 52.1 MB | 5.3 MB | 9.8× | 90 | 197 |
| Terrain | 170,534 | 104.7 MB | 5.5 MB | 19× | 213 | 200 |
| Dog | 124,418 | 136.5 MB | 6.5 MB | 21× | 111 | 262 |
| **Bear** | 694,272 | 995.6 MB | 7.1 MB | **140×** | 946 | 340 |

For comparison in the same table, **PFNN is 9.3 MB / 1,370 µs** and **MANN is 16.9 MB / 2,440 µs** —
LMM is 4-7× faster than the phase-functioned family at comparable memory.

A 34-subject user study inside a shipping Ubisoft production found "no preference in terms of
quality" and a "slight preference toward Motion Matching in terms of responsiveness"; experienced
animators could not reliably tell them apart.

### 3.2 The property that matters most, and it is not memory

**At runtime there is no database at all.** State is `(x, z, pose, blend)` — a few hundred floats.
Evaluation is a chain of dense matmuls with ReLU. No search, no tree traversal, no data-dependent
branching, no allocation.

For AV Gen that makes LMM **more deterministic than classical motion matching**, not less: a
nearest-neighbour search has tie-breaking that depends on acceleration-structure build order, and a
matmul does not. It sits cleanly on the ADR-360 "a render must be reproducible" side of the line.

The cost is debuggability: there is no frame index to show an artist. The paper's own mitigation is
architectural and survives a port — because the three networks are separable, you can run the
Decompressor alone over a *real* feature and latent database ("DMM", costed in the same table) and
A/B against full LMM to localise a defect to the Stepper or the Projector. **Any AV Gen
implementation should keep DMM mode as a diagnostic build.** That is the answer to Part 21's
black-box objection.

Also relevant: the authors state they "actively encourage our system not to generalize as the
Projector emulates the nearest neighbor search." LMM does not invent motion. It compresses a
database. If AV Gen wants new motion, that is §9's problem, not this one.

### 3.3 The training cost, stated plainly

500,000 iterations, batch 32, RAdam, lr 0.001 decayed ×0.99/1000 iters, `z ∈ R^32`. The authors
trained **single-threaded on CPU** because "the small network sizes made training on the CPU almost
always more efficient than training on the GPU"; total 14.7-29.3 hours per scenario; "training
overnight is required for optimal results". Order: Decompressor first (it produces `Z`), then
Stepper and Projector in parallel.

Decompressor accuracy: mean joint positional error **1.4 cm, σ 1.1 cm**. The loss is the crux and
the naive one fails — plain MSE gives "jittery, low quality motion"; the working loss is L1 in both
local and character (FK) space plus velocity losses plus L1/L2/velocity regularisers on the latent.

### 3.4 The runtime is 173 lines and has no ML framework

`nnet.h` in Holden's repo is a `struct nnet { input_mean, input_std, output_mean, output_std,
vector<array2d<float>> weights, vector<array1d<float>> biases }` loaded from a flat binary, with a
matmul that exploits ReLU sparsity and preallocated activations. Its own comment: *"This appears to
be the fastest way I found to do basic matmul on the CPU. It takes advantage of activations set to
zero due to relu and compiles well to SIMD. It's important for performance that the pointers are
labelled restrict."* Zero heap traffic per frame.

**This is the answer to Part 10's "no Python at runtime, no PyTorch in the renderer" requirement,
and it is 173 lines, MIT, from the paper's author.** See §10 for why that beats ONNX Runtime here.

### 3.5 The other three repos

* **[pau1o-hs/Learned-Motion-Matching](https://github.com/pau1o-hs/Learned-Motion-Matching)** — no
  LICENSE file at all (default copyright, legally unusable), abandoned 2022-12-28, Unity+Barracuda,
  ships an 18.7 MB ASCII float dump as its database. Ignore.
* **[E1P3/Learned_Motion_Matching_UE5](https://github.com/E1P3/Learned_Motion_Matching_UE5)** — no
  LICENSE, 1,075 LOC, deeply Unreal-coupled (NNE/ONNX Runtime, `FAnimNode_Base`, PoseSearch,
  nanoflann, Eigen), and its README documents an unfixed bug: *"Run this project from the IDE in
  debug mode rather then in development mode. For some reason the file parser does not work
  natively."* Useful only as evidence that LMM ports cleanly onto an ONNX runtime.
* **[E1P3/Learned_Motion_Matching_Training](https://github.com/E1P3/Learned_Motion_Matching_Training)**
  — **MIT**, © 2024. A fork of Holden's training scripts with `save_onnx_network` substituted for
  the binary writer. Its FBX converter is Windows-only, needs the Autodesk FBX SDK, and its
  `AnimDataExtraction.cpp` is a **0-byte file**.

### 3.6 Verdict

**Learned motion matching is the right *eventual* runtime and the wrong *first* build.** It buys
nothing until there is a database big enough for its compression to matter — and AV Gen's database
would have to grow by three orders of magnitude before 5 MB of weights beat 0.18 MB of features.
But it is worth designing towards, because the feature extraction, the database build and the
offline pipeline are **identical** for classical and learned MM. Build the pipeline once; the
learned runtime is a later, additive choice.

---

## 4. AI4Animation, the Neural State Machine, and SAMP

### 4.1 The licence finding, first, because it governs everything else

**[AI4Animation](https://github.com/sebastianstarke/AI4Animation) has no LICENSE file.**
`GET /repos/sebastianstarke/AI4Animation/license` returns 404. The only licensing statement is one
paragraph in the README, verbatim:

> **Copyright Information**
> This project is only for research or education purposes, and not freely available for commercial
> use or redistribution. The motion capture data is available only under the terms of the
> Attribution-NonCommercial 4.0 International (CC BY-NC 4.0) license.

So the brief's expectation is confirmed for the data and is **too generous for the code**: the code
has no named licence, no patent grant, no warranty disclaimer and no defined derivative-works
clause — only that sentence. That is default copyright plus an informal grant.

The 2026 Python remake is a **separate repository**,
[facebookresearch/ai4animationpy](https://github.com/facebookresearch/ai4animationpy) (created
2026-02-03, last push 2026-08-14), and it carries a real LICENSE file: **CC BY-NC 4.0 applied to
the code itself**. It removes the Unity dependency (NumPy/PyTorch/raylib end to end, Python ≥
3.12.12) and it does **not** contain NSM, PFNN, MANN or LMM — it ships locomotion controllers and
the Codebook Matching machinery from SIGGRAPH 2024.

**Conclusion: the entire Starke line, 2017 through 2026, code and data alike, is non-commercial.**
Nothing from it can be vendored, and models trained on its data inherit the restriction. It is
reading material.

### 4.2 What is actually in the repository

Seven directories, verified against the GitHub tree, not the README: `SIGGRAPH_2017` (PFNN),
`SIGGRAPH_2018` (MANN, quadruped), `SIGGRAPH_Asia_2019` (NSM), `SIGGRAPH_2020` (Local Motion
Phases), `SIGGRAPH_2022` (DeepPhase), `SIGGRAPH_2024` (Categorical Codebook Matching), `Plugins`.
**There is no `SIGGRAPH_2021` directory** — Neural Animation Layering is listed in the README with
paper and video but no code.

### 4.3 Neural State Machine, in enough detail to judge it

Starke\*, Zhang\*, Komura, Saito, *Neural State Machine for Character-Scene Interactions*, TOG
38(6) art. 178, SIGGRAPH Asia 2019.

**Inputs** `X = {F, G, I, E}`:
* `F` — 23 joints' positions/rotations(6D)/velocities, plus a 13-point root trajectory over a
  2-second window (−1 s..+1 s) with 2D positions, 2D facings and **continuous** action labels over
  seven types: idle, walk, run, sit, open, carry, climb.
* `G` — goal positions (3D), directions (2D) and **one-hot** action labels at the same 13 points.
* `I` — the **Interaction Sensor**: an **8×8×8 volume** over the target object, each element a 4-vector
  (relative position + soft occupancy).
* `E` — the **Environment Sensor**: a *cylindrical* occupancy volume around the character, flattened
  to R^1408. The concrete (radius, height, sphere radius) are **not stated in the paper**.

**Outputs**: pose, predicted pose 1 s ahead, a 6-point future trajectory *twice* (egocentric and
goal-centric — the "bi-directional control scheme"), updated goal parameters, **5 contact labels**
(feet, hands, hip), and a **1-D phase update**.

**Architecture**: a 3-layer 512-wide ELU prediction network whose *entire weight set* is blended
per frame from **K = 8 or 10 experts**, `α = Σ ωᵢ αᵢ`, with the gate a 3-layer 128-wide softmax
over the Kronecker product of a 2-D phase vector `{sin p, cos p}` with `{current action, goal
action, goal action × distance-to-goal, goal action × angle-to-goal}`.

**The data cost is the wall, and it is not close.** 94 minutes of XSens mocap. **Every frame is
hand-labelled** with an action, a goal action, *and a phase scalar*. Scene augmentation fits
template objects, embeds contact points, samples ShapeNet replacements (20 chairs, 2 desks, 3
boxes, 2 windows), and re-solves the pose with CCD full-body IK — and when contact re-projection
fails, "we manually specify the new location on the object surface." Training set 16 GB, 70 epochs,
**~20 hours on a GTX 1080 Ti**, resulting network **~250 MB**.

Adding one new interaction class is a mocap shoot with physical props, a per-frame labelling pass
including phase, a template fit, a ShapeNet pool with hand-corrected contacts, and a full retrain.
That is person-weeks to person-months per interaction, with a human in the loop. The 2020 Local
Motion Phases and 2022 DeepPhase papers exist specifically because phase labelling was untenable.

**Failure modes, from the authors:** it "fails to adapt to geometry that is rather different from
that in the training set"; discontinuous goal switching produces "abrupt movements", mitigated only
by a 0.25-0.5 s input smoothing window that trades directly against responsiveness; without the
Environment Sensor the character "penetrates through" obstacles; the README warns of "many corner
cases where the system may fail due to the exponential combinatorial amount of possible actions".
SAMP measures NSM's motion **Diversity at exactly 0.0** — one goal, one motion, always — and its
object penetration at **8.11%** for sitting.

The paper states **no per-frame runtime in ms**. MANN, a smaller member of the same mixture-of-
experts family, was measured at **2,440 µs/frame** in the LMM paper; NSM is larger. A low-single-
digit-millisecond CPU frame cost is a reasonable estimate and is **not verified**.

Notably, NSM's Unity runtime is already just Eigen matmuls over raw `.bin` weight matrices
(`NSM.cs`, `NativeNetwork.cs`) — **no TensorFlow or ONNX at runtime**. The port would be a day's
work. The licence is what stops it, not the engineering.

### 4.4 SAMP, and a correction to the brief

Hassan et al., *Stochastic Scene-Aware Motion Prediction*, ICCV 2021.

* **GoalNet** — a small cVAE from an object's occupancy grid to `(goal position, goal direction)`.
  Sampling `Z ~ N(0, I)` gives *multiple distinct* valid places to sit on one sofa. Reported
  reconstruction error on 150 unseen goals: **6.04 cm, 2.29°**.
* **MotionNet** — an autoregressive cVAE whose decoder is itself a 12-expert mixture (shipped
  `MotionNet.yaml`: `num_experts 12, z_dim 64, h_dim 512, state_dim 647`). `Z` is concatenated into
  *every* layer. **Scheduled sampling is mandatory** — plain supervised training "produces poor
  quality predictions at run time" from error accumulation.
* **The scene encoding is NOT a basis point set.** The brief guesses BPS; grepping the paper for
  "basis point"/"BPS" returns zero hits. It is the same **8×8×8 voxel grid of 4-vectors** NSM uses.
* **"A\* path planning" is `NavMesh.CalculatePath`.** `PathPlanningUtility.cs` is
  `using UnityEngine.AI;` → `NavMesh.SamplePosition` → `NavMesh.CalculatePath` → take
  `Path.corners`. The README confirms a NavMesh must already be baked. **AV Gen's own `NavGrid` A\*
  with string-pulling is a strictly better-documented version of this** and already exists.

**Licence**: the code README says you must adhere to AI4Animation's terms *and* adds "You may not
redistribute the Research Materials." The dataset licence (Max-Planck) forbids commercial use,
forbids distribution beyond one archival copy, and **explicitly prohibits using the dataset to
train methods for commercial use** — a viral clause that follows the weights.

**Is the stochasticity a determinism problem?** No — it is one `Z ~ N(0,I)` draw per frame and one
per goal. Replace the RNG with AV Gen's PCG32 keyed on (entity seed, frame index) and it is exactly
reproducible while still varied across entities. This is the same pattern ADR-091 already uses. The
real cost is that a KL-balanced cVAE with scheduled sampling is much harder to *train* than a
deterministic net, and it inherits NSM's penetration and precision problems (5.38° sit rotation
error vs NSM's 2.32°).

### 4.5 What to take from this family

| idea | take it? | why |
|---|---|---|
| Goal + trajectory + pose as a single autoregressive input | **Yes, as a design idea** | It is the right decomposition and it costs nothing to adopt in the `MotionRequest` API (§11) |
| Voxel occupancy volume as a scene encoding | **Yes, eventually** | AV Gen already has `ClearanceField`, `ObstacleField` and `NavGrid`; a local 8×8×8 occupancy sample is cheap on top of those |
| **GoalNet's question** — "where on this object, facing which way" | **Yes, as a deterministic function first** | For a mushroom or a rock, "stand 1.5 m away facing it" is arithmetic. A learned version is only needed for objects with non-obvious affordances |
| Bi-directional control (blend the network's goal output with the user's) | Note it | It is what stops the character mis-aligning on the chair; any goal-driven system needs the equivalent |
| Mixture-of-experts gated by phase | **No, not now** | 8-10× the weights, rebuilt every frame, for a 94-minute hand-labelled dataset AV Gen does not have |
| Any of the code | **No** | Non-commercial, and in SAMP's case explicitly non-redistributable |

---

## 5. Procedural IK and contact

### 5.1 What exists

`solveTwoBone` is a correct, well-argued analytic two-bone solver with an honest status enum
(`src/scene/ik.hpp`). `plantOnPlane` + `footAlign` + `soleUp`-from-rest-pose is a complete foot
contact model for a *plane* (`src/scene/pose_layers.hpp:128-170`). The `Aim` layer is a look-at
with yaw/pitch limits about a named pivot. `GroundFollower` supplies the plane and owns the
smoothing, at the right level (`src/entity/grounding.hpp:88-91`).

### 5.2 What is missing, in priority order

1. **A leg the solver can see on the alien** (§1.10). Everything else here is downstream of this.
2. **Per-foot ground sampling.** `LocomotionState` carries **one** ground plane for the whole body
   (`groundPoint`, `groundNormal`). A body straddling a root or standing across a step needs a
   plane per foot. The layer already drops each foot onto the plane *under itself*, so the plane is
   the bottleneck, not the solver.
3. **Contact/phase knowledge.** Nothing knows when a foot is planted. Without it, foot IK corrects
   a *swinging* foot as hard as a planted one, which reads as the foot sticking. The standard fix
   is to weight the correction by a per-foot contact signal, and that signal has to be either
   authored or detected offline (§8).
4. **The known −Y approximation.** `plantOnPlane` drops along model-space −Y, which leans with the
   body's `slopeAlign` tilt; `pose_layers.hpp:299-305` measures the drift at ~3 cm on a 0.2 m
   correction at a 10° lean and records it as an ADR-359 revisit trigger.
5. **Hand/reach IK.** The same solver works; there is no arm chain wired, no `Reach` layer kind,
   and no target source.
6. **Hip/pelvis adjustment.** Real ground adaptation lowers the hips when a leg cannot reach. AV
   Gen clamps and reports `Clamped` instead, which is honest but visibly wrong on a step.
7. **Spine/look chain distribution.** The `Aim` layer turns a group of joints rigidly about one
   pivot. A believable look distributes rotation down the spine.

**This layer should stay deterministic and should stay the last word.** The brief says it and the
architecture already enforces it: no learned system should be asked to get contact right to the
millimetre, and AV Gen's IK already cannot remember anything across a frame.

---

## 6. Physics and secondary motion

AV Gen has **no physics of any kind**: no rigid bodies, no contacts, no integrator, no solver.
A character's collision shape is a vertical cylinder of `bodyRadius` (default 0.45 m) and
penetration is resolved by pushing out, clamped so a deeply embedded body walks out over several
frames (`docs/character-animation-lab.md`, "Does collision modify velocity...").

What AV Gen has *instead* is `liveliness` (ADR-198) — a procedural secondary-motion behaviour
supplying bounce, stride, sway and nod that the clips lack — and it is on all 21 animated
characters in the multicam scene.

**Recommendation: do not build physics for this.** Ragdolls, partial-body physics and dynamic
balance are a large, stateful, notoriously non-deterministic subsystem whose payoff for an
audiovisual application is reactions the `Additive` pose layer can already fake. The one physics-
adjacent thing worth building is **spring-damper secondary motion on a masked joint group**
(antennae, tails, cloth-like appendages), which is ~100 lines, has a closed-form critically-damped
solution, and — critically — is *state*, so it must live in the entity tier where a seek replays
it, not in `pose_layers` which is forbidden to remember. Holden's `spring.h` (214 LOC, MIT) has the
exact arithmetic including the inertialization springs.

---

## 7. Generative and diffusion motion

The question the brief asks — "should generative models run at runtime?" — has a clear answer:
**no**, and not primarily for performance reasons. The blocking reason is licensing, and it is
traced in §8.

The practical architecture the brief proposes is the right one:

```
OFFLINE:  generative model → synthesis → validation → retarget → cleanup → bake → motion database
RUNTIME:  motion database → matching/synthesis → IK → final pose
```

with one addition: **a rejection stage with a measurable criterion**, because generated motion's
characteristic failures (foot sliding, ground penetration, jitter) are exactly the things an
offline pipeline can measure and an artist cannot be asked to eyeball across hundreds of clips.
Foot-slide per frame, penetration depth, and joint-velocity spikes are all computable.

---

## 9. The proposed architecture

### 9.1 The shape

```
                    ┌──────────────────── OFFLINE (avgen-motion-build) ────────────────────┐
  source motion ──▶ │ import → skeleton normalise → RETARGET → resample → clean →          │
  (glb/fbx/bvh)     │ contact detect → phase extract → trajectory extract → feature        │
                    │ extract → segment → tag → validate/reject → pack                     │
                    └──────────────────────────────┬───────────────────────────────────────┘
                                                   │  character.motionpack  (versioned, on disk)
  ═════════════════════════════════════════════════╪═══════════════════════════════════════════
                                                   ▼  RUNTIME (no Python, no PyTorch, no training)
   ┌─────────────┐   ┌──────────────┐   ┌────────────────┐
   │ Perception  │──▶│  Selector    │──▶│  ActionQueue   │      ←── all three EXIST today
   │ (exists)    │   │ +Considerers │   │  (exists)      │
   └─────────────┘   └──────────────┘   └───────┬────────┘
                                                ▼
                                      ┌───────────────────┐
                                      │   MotionRequest   │   ←── NEW: the one new struct
                                      │ velocity, facing, │       (a superset of LocomotionState)
                                      │ look, style,      │
                                      │ action, contacts  │
                                      └─────────┬─────────┘
                                                ▼
                                      ┌───────────────────┐
                                      │  IMotionProvider  │   ←── NEW: the one new interface
                                      └─────────┬─────────┘
             ┌──────────────┬───────────────────┼───────────────────┬──────────────┐
             ▼              ▼                   ▼                   ▼              ▼
     ClipProvider    BlendSpaceProvider   MatchingProvider   LearnedProvider   ScriptedProvider
     (= today's      (NEW, phase-         (LATER)            (MUCH LATER)      (tests/timeline)
     AnimationPlayer  matched 1D/2D)
     unchanged)
             └──────────────┴───────────────────┴───────────────────┴──────────────┘
                                                ▼
                                          scene::Pose
                                                ▼
                        ┌───────────────────────────────────────────┐
                        │  PoseLayerStack  (EXISTS — Aim/Additive/  │
                        │  Foot, + NEW Reach, Spring, Spine)        │
                        └───────────────────┬───────────────────────┘
                                            ▼
                                     joint palette → GPU
```

### 9.2 The five rules this architecture is built on

1. **`IMotionProvider` is a sibling of `AnimationPlayer`, never a replacement.** Every character
   can run in Clip mode, and Clip mode is the fallback for every failure in §13.
2. **A provider is pure or it is not in `scene/`.** A provider that needs memory across frames
   (motion matching's "which frame am I on") keeps that memory in the **entity tier**, which
   `EntityWorld::seek` replays, and hands it to the provider each frame as part of `MotionRequest`.
   This is the single most important structural decision in the proposal: it is what keeps ADR-360
   and ADR-091 intact, and it is why the provider interface takes a *context* rather than owning
   state.
3. **The offline/runtime boundary is a file.** `character.motionpack` is the only thing that
   crosses it. Nothing at runtime reads a `.glb` for motion, trains anything, or calls Python.
4. **The IK/contact layer stays last and stays deterministic.** No provider is responsible for
   contact accuracy.
5. **No layer names a clip.** Already rule R4 (`src/entity/character_ai.hpp:57-60`). A
   `MotionRequest` names an *intent*; the motionpack maps intent to content.

### 9.3 Proposed C++ interfaces (conceptual)

```cpp
namespace avgen::motion {

// What the character wants. A superset of entity::LocomotionState, and the thing that replaces
// the polar (speed, yaw) representation that cannot say "moving NE while facing N".
struct MotionRequest {
    // ---- kinematic intent, in the entity's own frame ----
    glm::vec2 desiredVelocity{0.0f};   // m/s, XZ. Length 0 = stand.
    glm::vec2 desiredFacing{0.0f, 1.0f};
    // The predicted trajectory the provider matches against. Three samples is what the literature
    // uses; AV Gen already owns the two things needed to fill it -- Navigator::steer for the
    // direction and Gait::approach for the speed ramp.
    std::array<glm::vec2, 3> futurePosition{};  // +0.33 s, +0.66 s, +1.0 s, entity frame
    std::array<glm::vec2, 3> futureFacing{};

    // ---- what the body is doing, not how it moves ----
    entity::Activity activity = entity::Activity::Idle;
    std::string_view action;      // "sit", "pickUp" -- an activity NAME, never a clip name (R4)
    float urgency = 0.0f;         // 0..1; picks a style band, never a clip
    float energy = 1.0f;
    float caution = 0.0f;

    // ---- attention ----
    glm::vec3 lookTarget{0.0f};   // entity-local by the time it arrives (ADR-274)
    bool hasLookTarget = false;

    // ---- contact intent (the thing that is missing today) ----
    // Per foot, the plane under THAT foot, not one plane for the body.
    struct FootGround { glm::vec3 point; glm::vec3 normal; bool valid; };
    std::array<FootGround, 4> feet{};

    double time = 0.0;            // the timeline second, never a wall clock
};

// The provider's own memory, owned by the ENTITY (which a seek replays) and handed in each frame.
// This is what keeps a stateful provider on the right side of ADR-091.
struct MotionMemory {
    std::int32_t frame = -1;      // matching: the database frame currently playing
    float clipTime = 0.0f;
    std::uint64_t generation = 0; // which run of which selection, for root-motion differencing
    std::array<float, 64> latent{}; // learned: x and z, sized at build
    // Inertialization state: the pose offset being decayed away, and when it started.
    double transitionStart = -1.0;
};

class IMotionProvider {
public:
    virtual ~IMotionProvider() = default;
    [[nodiscard]] virtual std::string_view kind() const = 0;
    // Pure in (request, memory-in, skeleton, pack). Writes the pose and the NEXT memory.
    // Never allocates after bind(). Never reads a clock. Returns false to fall back (§13).
    [[nodiscard]] virtual bool evaluate(const MotionRequest& request, const MotionMemory& in,
                                        const scene::Skeleton& skeleton, scene::Pose& out,
                                        MotionMemory& next) = 0;
    // What it did, for the overlay. Structural quantities only (ADR-170): never a millisecond.
    [[nodiscard]] virtual MotionProviderDebug debug() const = 0;
};

// What the overlay draws. Deliberately the same shape for every provider.
struct MotionProviderDebug {
    std::int32_t selectedFrame = -1;
    std::string_view selectedClip;
    float selectedCost = 0.0f;
    std::array<std::pair<std::int32_t, float>, 8> runnersUp{}; // the losing candidates and scores
    std::uint32_t candidatesConsidered = 0;
    float transitionBlend = 1.0f;
    bool fellBack = false;
    std::string_view fallbackReason;
};

} // namespace avgen::motion
```

`runnersUp` is not decoration. It is the same argument `IBehavior::decisionDebug` and `ScoredOption`
already won (ADR-333): *"a hysteresis nobody can see is a hysteresis nobody can tune."* A motion
matcher without visible runners-up is the same defect one level down.

### 9.4 The motionpack format

```
character.motionpack                    (a directory, or a container; a directory first)
  pack.json         version, source skeleton hash, joint names, licence metadata, build provenance
  skeleton.bin      normalised skeleton: joints, parents, rest pose, bone lengths
  clips.bin         resampled poses, fixed rate, quantised (see below)
  features.bin      float32 [frames × D], normalised and weight-folded at build time
  meta.bin          per frame: clip id, local time, phase, contacts (bitfield), tags
  ranges.json       per clip: name, tags, loop flag, blend regions, authored stride speed
  model/            optional: network weights, one flat binary per net, plus normalisation stats
```

Two decisions worth arguing now:

* **Quantisation.** Holden's implementation stores the pose database as int16 and reports no
  quality loss. AV Gen's rig is 89 joints × (T,R,S) = 890 floats/frame = **3.5 KB/frame**
  uncompressed, i.e. 107 KB per second of motion at 30 Hz. At int16 with per-channel ranges that is
  1.8 KB/frame. The right first move is simpler: **drop the scale channels**, which are constant in
  every clip in this repository, and store rotations as quaternions with the sign-fixed smallest-
  three encoding. That is 89 × (3 + 3) × 2 bytes = 1.1 KB/frame, a 3.3× win, before any clever
  compression.
* **Licence metadata is a required field, not an optional one.** §8 explains why. A pack whose
  `pack.json` has no `license` key fails the build.

### 9.5 The offline/runtime boundary (Part 25)

| capability | offline | runtime | note |
|---|---|---|---|
| mocap / clip import (glb, fbx, bvh) | **yes** | no | runtime reads only `.motionpack` |
| skeleton normalisation | **yes** | no | |
| **retargeting** | **yes** | no | §12; does not exist in any form today |
| motion cleanup, resampling | **yes** | no | |
| **foot-contact detection** | **yes** | no | velocity+height threshold on the tip joint; a 30-line pass |
| **phase extraction** | **yes** | no | from contacts; needed for §10's phase-matched transitions |
| trajectory extraction | **yes** | no | requires root motion in the source (§1.8) |
| feature extraction + normalisation | **yes** | no | weights folded in at build (§2.1) |
| motion segmentation / tagging | **yes** | no | |
| neural training | **yes** | **never** | PyTorch is a build tool, like a shader compiler |
| model conversion / export | **yes** | no | flat binary, not ONNX (§10) |
| nearest-neighbour index build | **yes** | no | not needed below ~100k frames (§2.4) |
| database quantisation | **yes** | no | |
| validation / bad-motion rejection | **yes** | no | foot-slide, penetration, velocity spikes |
| **motion-matching query** | no | **yes** | 11-132 µs at AV Gen's scale (§2.3) |
| **neural inference** | no | yes (later) | 173-line hand-written matmul (§10) |
| **IK / contact solving** | no | **yes** | exists |
| inertialization | no | **yes** | new; ~150 lines |
| goal evaluation / considerers | no | **yes** | exists |
| path planning (A\*) | no | **yes** | exists; grid built once, `rebuildRect` for changes |
| generative / diffusion synthesis | **yes** | **never** | §7, §8 |
| motion variation generation | **yes** | no | mirroring, time-warp, trajectory edit — DERIVED, not synthesised |
| animation compression | **yes** | no | |
| debug visualisation | both | **yes** | overlay reads `MotionProviderDebug` |

### 9.6 Where the work runs

Everything in the runtime column is on the main thread today and should **stay there for now**.
The entity update is single-threaded by assumption in two places that would silently corrupt if
that changed: `NavGrid`'s A\* scratch is `mutable` and documented as not reentrant
(`src/entity/nav_grid.hpp:349-351`), and `GridPerception::perceive` says outright that it is not
safe from two threads (`src/entity/perception.hpp:98-101`).

The honest threading opportunity is **rig posing**, not motion matching: `updateRigs` loops over
independent rigs, each writing only its own `pose`/`palette`, and already measures itself
(`RigStats::cpuMs`, read by nobody). That is a clean `parallel_for` with no shared state — and at
21 characters × ~3 µs it is not worth doing yet. **Measure `RigStats::cpuMs` in a panel before
threading anything.**

`avgen-motion-build` runs offline, and `src/app/job_system.hpp` already provides staged progress,
honest ETA and prompt cancellation for exactly this.

---

## 11. What to build: the classification (Part 23)

Evidence-based, and several of these differ from the brief's own examples.

### BUILD NOW

| item | why | cost signal |
|---|---|---|
| **Cross-file clip binding + retargeting (offline)** | The single blocking dependency for everything else, including "just add more clips". Today a character's motion is whatever shipped in its own `.glb` (`src/assets/gltf_loader.cpp:342-364`) | §12 prices it. It is the largest single unit in this proposal |
| **Contact + phase extraction (offline)** | 30-line pass. Unlocks phase-matched transitions, foot-IK weighting, and every later feature | small |
| **Phase-matched transitions** | `play()` starts every clip at phase 0 (`src/scene/animation.cpp:251`). Walk→Run currently lands on a random foot. This is the cheapest visible quality win in the whole document | small; a `float phaseOffset` on `AnimationState` and an offset in `localTime` |
| **Inertialization** | Replaces cross-fading with a decaying pose offset. Closed-form, stateless given (offset, start time, duration) — so it fits AV Gen's "store *when*" rule exactly | ~150 lines; Holden's `spring.h` (MIT) has the arithmetic |
| **A leg the alien's foot solver can see** | §1.10. Foot planting, terrain adaptation and step-over are all blocked on it | §12.2 |
| **Per-foot ground planes** | `LocomotionState` carries one plane for the body; `GroundFollower` already samples a footprint ring (`grounding.cpp:60-65`) and throws the per-sample results away | small |
| **A velocity *vector* on `EntityState`** | Polar (speed, yaw) cannot express strafing, backing up, or facing≠heading. Every later system wants it | medium: wide but shallow |
| **Surface `RigStats::cpuMs` and `PoseLayerStats` in a panel** | Already measured, read by nobody. Part 17 cannot be answered without it | trivial |
| **A motion-authoring panel** | There is no animation UI at all (§1.7). Without one, everything below is authored in JSON by hand | medium |

### BUILD LATER (in this dependency order)

| item | blocked on | why later |
|---|---|---|
| **1D/2D blend spaces** (walk↔run, and directional) | phase extraction | Gives most of motion matching's smoothness for a fraction of the machinery, and works on 3 clips where MM needs 30 minutes. `docs/character-ai-research.md:677` rejected this on grounds that no longer hold |
| **`IMotionProvider` + `MotionRequest`** | velocity vector | The seam. Cheap once the representation is right |
| **Classical motion matching** | retargeting, a real dataset, inertialization | §2. The algorithm is easy; the data is not |
| **Reach / hand IK, hip adjustment, spine look distribution** | per-foot planes | Pure extensions of the existing layer stack |
| **Spring secondary motion (antennae, tails)** | — | ~100 lines, but it is *state*, so it must live in the entity tier |
| **Scene-aware interaction goals** ("stand here, face this") | `MotionRequest` | Deterministic arithmetic first; a learned GoalNet only for non-obvious affordances |
| **Offline motion variation** (mirror, time-warp, trajectory edit) | motionpack | DERIVED motion, clearly labelled as such |
| **Rig-posing on the job system** | a measurement showing it matters | §9.6 |

### RESEARCH ONLY

| item | why |
|---|---|
| **Learned motion matching** | §3.6: right eventual runtime, wrong first build. Revisit when the database exceeds ~100,000 frames or ~50 MB |
| **Neural State Machine / mixture-of-experts control** | §4.3: 94 minutes of *per-frame hand-labelled* mocap including phase, 20 GPU-hours per retrain, 250 MB of weights, 8% object penetration, zero motion diversity — and a licence that forbids use |
| **Generative / diffusion motion synthesis** | §7-8: the output's licence depends on the training set, and almost every open model traces to AMASS |
| **Learned GoalNet** | Useful idea; the deterministic version answers the question for mushrooms and rocks |

### DO NOT USE

| item | why |
|---|---|
| **Any AI4Animation or SAMP *code* or *data*** | §4.1, §4.4. No licence, non-commercial, and in SAMP's case explicitly non-redistributable |
| **A physics character controller / ragdoll** | §6. No physics exists; the payoff is reactions the additive layer already fakes; determinism cost is severe |
| **Runtime generative models** | Latency, determinism, licensing, and no runtime need |
| **A new entity system, a behaviour tree, GOAP/HTN** | Already rejected with reasons in `docs/character-ai-research.md` §F.2, and the reasons still hold |
| **ONNX Runtime for these networks** | §10 |
| **An LLM anywhere in the animation core** | Explicit in the brief and correct |
| **A nearest-neighbour acceleration structure** | §2.4: brute force wins at AV Gen's scale and costs no determinism |
| **The early-out inner loop** | §2.3: measured 2.0-2.7× *slower* on this machine |

---

## 12. The two costs that are not "N weeks"

The brief asks for concrete cost. These are the two units where the cost is structural rather than
schedular, so here is what each one actually has to do.

### 12.1 Retargeting: what a pass that does not exist has to do

Today `Importer::importClips` pushes each glTF animation onto the rigs built in the same
`loadGltf` call and nothing else (`src/assets/gltf_loader.cpp:342-364`). There is no joint-name
map, no bind-pose reconciliation, no bone-length scaling, no hierarchy remap anywhere in `src/`.

A retargeting pass for AV Gen must:

1. **Map joints between two skeletons.** Not by name: the three rig families in this repository
   share no joint name (`src/scene/skeleton.hpp:88-94` — the alien calls the head `head.x`, the
   bull `Head01`, the chicken `Head`). So it needs an explicit map, authored once per rig pair, and
   a *humanoid role* vocabulary (hips, spine, neck, head, upper/lower arm, hand, upper/lower leg,
   foot, toe) to author it against. This is the part with no shortcut.
2. **Reconcile bind poses.** Source and target rest poses differ in joint orientation. The standard
   answer is to compute, per joint, `Δ = targetRestModel⁻¹ · sourceRestModel` and conjugate every
   animated rotation by it. Needs both rest poses in model space, which `poseToModel` already gives.
3. **Scale translations by bone length.** The alien is 1.66-1.79 m in file units; an arbitrary
   imported rig will not be. Root translation scales by hip height ratio; per-joint translations
   scale by the parent bone length ratio.
4. **Handle a rig whose joints are not a hierarchy.** §1.10: the alien's leg is three branches.
   A retarget that assumes "rotate the parent and the child follows" produces a character that comes
   apart, in exactly the way `root_motion.hpp` documents for the naive root-zeroing approach.
5. **Handle every joint carrying T, R and S in every clip** (§1.8). Blending or retargeting
   translation channels naively changes bone *lengths*. The pass must either zero non-root
   translations after retargeting (correct for a rigid skeleton) or preserve them deliberately.
6. **Report what it could not do,** in the style the rest of this codebase already uses
   (`JointMask::missing`, `LayerResolution`, `IkStatus`, `PathStatus`). A silent retarget is the
   failure mode this codebase has paid for repeatedly.
7. **Be offline and produce a file.** It must not run at load.

There is a real alternative worth pricing against it: **do not retarget; require content on the
target rig.** That is what the engine does today, and for a CC0 pack with 26 clips it worked. It
stops working the moment anyone wants a second motion source, and it means every new character
needs its own full animation set.

### 12.2 A leg the solver can see

Two options, and they are genuinely different projects.

**(a) Re-export the alien with a nested deform hierarchy.** `tools/export_alien_variant.py` and
`tools/make_aliens.sh` exist and the source `.blend` is Auto-Rig Pro. The export currently keeps
the 89 deform bones and drops 289 control/IK/`*_ref` bones. The deform bones are flat because
Auto-Rig Pro's deform layer is flat. Re-parenting `leg_stretch.l` under `thigh_twist_2.l` and
`foot.l` under `leg_twist_2.l` in the exporter would produce a chain the solver accepts — **but it
changes every joint's local transform**, so all 26 clips must be re-baked, and every existing
`palette`, socket offset and `soleUp` resolution changes with it. This is an asset-pipeline change
with a full-content regression, and the source `.blend` is not in the repository.

**(b) Extend the solver's write-back to non-ancestor chains.** `solveTwoBone` already works on three
*model-space positions* and returns two model-space pre-rotations (`src/scene/ik.hpp:76-97`); it is
only `PoseLayerStack::bind`'s ancestor check and the local write-back that require a chain. The
layer already computes model matrices for the whole pose (`model_` scratch), so it could write each
of the three joints' model transforms explicitly and convert each back through *its own* parent.
This is contained, testable, and needs no asset change — but it needs a new
`LayerResolution`/`IkStatus` answer for "these three joints move independently, so the intermediate
joints do **not** ride along", which is the property the current chain check exists to guarantee
(`src/scene/ik.hpp:34-38` — "the solve treats root→mid and mid→tip as rigid segments, which is
exactly true for any intermediate joint the solve does not itself write").

**(b) is smaller than it looks, and reading the write-back is what says so.** The code already
names the alien case itself, in `bind`:

> *"on `alien-scout.glb` the obvious three -- `thigh_stretch.l`, `leg_stretch.l`, `foot.l` -- are
> three separate branches under two different parents, and a solver handed them would happily
> produce rotations for a limb that does not exist."*
> — `src/scene/pose_layers.cpp:276-279`

And the write-back already writes **all three joints' locals explicitly**, each converted back
through its own parent (`pose_layers.cpp:505-531`). The only place ancestry is assumed is one
lambda: `parentModel(m, afterRoot)` applies the hip's rotation to the *mid joint's parent* on the
grounds that the parent inherits it. On a nested rig that is right. On the alien, `leg_stretch.l`'s
parent is the armature, which inherits nothing from `root.x`.

So the change is: **apply the accumulated pre-rotation to a joint's parent only when that parent
descends from the joint the rotation pivots about, and use the untransformed parent otherwise** —
plus relaxing `bind`'s two `descends` checks into a *reported* property rather than a refusal, so
that "the intermediate joints do not ride along" is something the layer says out loud (the same
shape as `JointMask::nested`, which already counts exactly this hazard for aim layers,
`src/scene/skeleton.hpp:120-124`).

On the alien that yields a working chain: `thigh_twist.l` turns and carries `thigh_stretch.l` /
`thigh_twist_2.l` with it; `leg_stretch.l` and `foot.l` are written explicitly and carry
`leg_twist*.l` and `toes_01.l` with them. Nothing is left behind.

**(b) is the right first move** and (a) is the right eventual one. (b) can be validated by the
prototype in §16 without touching production.

---

## 13. Failure and fallback (Part 22)

A ladder, with each rung a thing that already exists:

| failure | falls back to | mechanism |
|---|---|---|
| neural model missing / version mismatch | motion matching | `pack.json` version check at load; `IMotionProvider::evaluate` returns false |
| motionpack missing / hash mismatch with the rig | `ClipProvider` (today's behaviour) | the provider is chosen at bind, not per frame |
| motion-matching query finds nothing above threshold | hold the current frame, then the gait's clip | `MotionProviderDebug::fellBack` + reason |
| IK chain unresolvable | the animated pose, unchanged | **exists**: `LayerResolution::NoChain` / `Degenerate` |
| IK target out of reach | limb extended to its limit, reported | **exists**: `IkStatus::Clamped` |
| path unreachable | `Navigator::steer` local avoidance | **exists**: `PathStatus::Unreachable` |
| nav grid does not trust itself | analytic `Navigator::sample` | **exists**: `NavGrid::vouches()` |
| everything | the rest pose, never a T-pose | **exists**: `sampleClip` writes over a seeded rest pose |

The one new rule: **a provider that returns false must say why, and the reason must be visible.**
That is the same rule `LayerResolution`, `IkStatus`, `PathStatus`, `SocketResolution` and
`ActionResult` already follow, and it is why this codebase can answer "why did nothing happen".

---

## 14. Editor UX and debugging (Parts 21, 28)

### 14.1 What to draw

`rendering::DebugViewOptions` already has 25+ switches including `skeletons`, and
`labs::overlaysFor(LabId)` already selects a per-lab subset. The new switches:

| switch | draws |
|---|---|
| `motionTrajectory` | the three future trajectory samples of `MotionRequest`, as points and facing arrows, plus the *matched* trajectory in a second colour. When they diverge, the query is wrong |
| `motionCandidates` | the runners-up from `MotionProviderDebug`, as labelled costs |
| `motionPhase` | the current phase as a dial, with contact events |
| `footContacts` | per-foot: the sampled plane, the IK target, the achieved tip, and the `IkStatus` |
| `motionRequest` | the request as text at the character: activity, velocity, facing, urgency, look target |
| `decisionScores` | **exists as data** (`DecisionDebug`, `ScoredOption`) with no overlay |

### 14.2 The panel that is missing

A Character panel with: the rig and its clips; the pose-layer stack with per-layer
`LayerResolution` and joint counts; gait settings with the live `footSlip` ratio; the provider in
use and its fallback state; and `RigStats`. Every one of those values is already computed and
thrown away.

### 14.3 The test environment

`examples/labs/character/` already has `character-intelligence-lab`, `guard-post`,
`river-crossing` and `perception-crowd`; `examples/labs/footik/` has the foot-IK pair. A motion lab
belongs beside them, not as a new application (ADR-261). It needs: flat ground, a graded slope, a
step field, scattered rocks and roots, a mushroom to inspect, and one alien — with the test
commands the brief lists driven from the existing `ActionQueue` (`Move`, `Face`, `Pose`,
`Interact` already exist; `Sit`/`Stand`/`Jump` are `Pose` with an activity name).

**Do not test this in `glowmere-valley-2-multicam`.** That scene is the acceptance target, not the
diagnostic.

---

## 15. Phased plan

The brief's phases are close to right. Two changes: Phase 1 is bigger than "animation metadata"
because retargeting lives in it, and phase-matched transitions + inertialization move *ahead* of
motion matching because they are the majority of the perceived quality win at a fraction of the
cost.

| phase | contents | depends on | unblocks |
|---|---|---|---|
| **0** | this document | — | everything |
| **1a** | Contact + phase extraction, offline. Phase-matched transitions. Inertialization. Per-foot ground planes. | — | 1b, 3 |
| **1b** | The non-ancestor IK chain (§12.2b). Foot planting on the alien, in a lab. | 1a | 3, 5 |
| **1c** | `EntityState` gains a velocity vector; `MotionRequest` defined; `IMotionProvider` introduced with `ClipProvider` as the only implementation and **identical output** to today. | — | 2, 4, 6 |
| **2** | `avgen-motion-build` v1: import → normalise → **retarget** → resample → contacts → phases → pack. `ClipProvider` reads a motionpack. Licence metadata mandatory. | 1a | everything after |
| **3** | Advanced procedural layer: reach/hand IK, hip adjustment, spine look distribution, spring secondary motion (entity tier). | 1b | 5 |
| **4** | Blend spaces (1D speed, 2D directional) as `BlendSpaceProvider`. | 1a, 1c | 6 |
| **5** | Scene-aware motion: per-foot terrain sampling from `NavGrid`, step-over, interaction goal points ("stand here, face this"), obstacle-aware approach. | 1b, 3 | 9 |
| **6** | Classical motion matching as `MatchingProvider`, **gated on a dataset existing**. | 2, 1a, 4 | 7 |
| **7** | Offline motion variation (mirror, time-warp, trajectory edit) — DERIVED motion, labelled. | 2 | 6 quality |
| **8** | Learned motion matching, **gated on the database exceeding ~100k frames**. | 6 | — |
| **9** | Autonomous goals: extend the considerer set toward the brief's examples. Mostly already possible. | 5 | — |
| **never** | Runtime generative models, physics ragdoll, AI4Animation/SAMP code or data. | | |

Phases 1a, 1b and 1c are independent of each other and of phase 2's retargeting. That is deliberate:
**the whole of phase 1 delivers visible quality on the content that exists**, and if the project
stops there it has still gained.

---

## 16. The prototype: the smallest experiment that validates or kills this

**One throwaway offline tool and one lab scene. No engine changes. No production animation path
touched.**

### The experiment

Write `tools/motion_probe.cpp` (a standalone target, like `tools/charai_probe.cpp` which already
exists and which its own plan says to *delete when the plan it priced is built*). It:

1. Loads `assets/aliens/alien-scout.glb` through the existing importer.
2. **Detects foot contacts** on `foot.l`/`foot.r` by tip height and velocity threshold over every
   clip, and prints them.
3. **Extracts a phase** for `Walking` and `Running` from those contacts.
4. **Synthesises a root trajectory** for each locomotion clip from `GaitSettings::walkSpeed`/
   `runSpeed` — the authored stride speed the engine already carries because the clips are in place
   (`src/entity/gait.hpp:102-105`) — and reports the implied per-frame trajectory features.
5. **Builds the 27-feature database** from the resulting 1,712 frames and runs a query loop.
6. **Solves the alien's left leg as a non-ancestor chain** (§12.2b) in model space, off to one
   side, and reports the `IkStatus` and the tip error.

### What it proves or kills

| result | conclusion |
|---|---|
| Contacts are clean and phases are stable | Phase-matched transitions and contact-weighted foot IK are buildable now. This is the load-bearing claim of phase 1a |
| Contacts are noisy or ambiguous | Every downstream feature needs authored contacts, and the offline pipeline grows a manual stage. **Price phase 1 higher.** |
| Non-ancestor two-bone solve lands the tip within a millimetre | §12.2(b) is the answer and the alien can have feet without an asset re-export. **This is the single highest-value bit.** |
| It cannot | The alien needs a re-export (§12.2a) and the whole contact half of the roadmap is gated on an asset pipeline change with a full-content regression |
| The 1,712-frame feature database gives plausible nearest neighbours for a synthetic query | Motion matching's *machinery* is sound; only the data is missing, and the roadmap is right |
| It does not — the matches are arbitrary because the synthesised trajectories are degenerate | Confirms §1.8 decisively: **motion matching is not reachable from in-place clips, at any effort**, and phases 6 and 8 should be cut until there is travelling content |

**Why this is the right probe** (ADR-182): every arm can fail, and three of the six failure modes
would change the plan. It runs on the CPU with no GPU lock, costs no engine risk, and its output is
six numbers a human can argue with.

**What it must NOT do:** touch `src/scene/animation.*`, `src/scene/pose_layers.*`,
`src/entity/*`, or any scene file. It reads assets and prints.

---

## 17. Risks

| risk | severity | what it looks like | mitigation |
|---|---|---|---|
| **Content, not code, is the binding constraint** | **high** | Motion matching is built and looks worse than the gait machine, because 57 s is not a database | §16 tests it before the build. Gate phase 6 on a dataset |
| **The alien rig cannot be given feet** | **high** | §12.2 both options are expensive; the source `.blend` is not in the repo | §16 arm 6 answers it in a day |
| **Licensing contaminates the deliverable** | **high** | A shipped motionpack derived from AMASS/LAFAN1/Mixamo | Mandatory `license` in `pack.json`; a build that fails without it. §8 |
| **Retargeting silently degrades motion** | medium | Clips play but the character is subtly wrong; no test catches it | Report-what-you-could-not-do; a byte-identity regression arm over the existing 26 clips when the pack is built from the same rig (the ADR-337 pattern: 159 of 165 bit-identical) |
| **A stateful provider breaks the determinism contract** | medium | Scrub and render disagree; two renders disagree | `MotionMemory` lives in the entity tier by construction (§9.2 rule 2). Reuse `tools/charai_probe.cpp`'s play-vs-seek harness |
| **Main-thread cost grows invisibly** | medium | The editor gets slower and nothing says why | Surface `RigStats::cpuMs` *before* building anything (§11 BUILD NOW) |
| **Scope: this brief is five products** | **high** | Partial versions of nine subsystems, none shippable | Phase 1 is designed to be independently valuable and to ship alone |
| **No animation UI** | medium | Every feature is authored in JSON and only an engineer can tune it | The panel is in BUILD NOW for this reason |
| **Doc rot** | low, already happening | §1.12's six stale comments | Fix them as a by-product of phase 1 |
| **"Built but unreachable"** | medium | Foot IK shipped in ADR-359 and no production character has ever used it (§1.10) | Every phase's acceptance is a scene an author can open, not a passing test |

---

## 18. Acceptance criteria, against what exists (Part 27)

| criterion | status today | what closes it |
|---|---|---|
| **Locomotion from desired velocity + facing** | partial. `Gait` picks an activity from a *scalar* speed with hysteresis and dwell, and rate-matches the clip. There is no velocity vector and no facing≠heading | velocity vector (1c) + blend space (4) |
| **Transitions idle→walk→run→stop→turn→jump→land→run** | works, and the machine is tuned. The joins are cross-fades from an arbitrary phase | phase matching + inertialization (1a) |
| **Terrain with believable foot placement** | **structurally impossible on the alien** (§1.10). Works on the farm rigs in one lab scene | 1b, then per-foot planes (1a) |
| **Navigate around obstacles** | **works.** A\* + string pull + local steer + crowd separation + clearance field | — |
| **Approach an object and interact** | **works** at the action level (`Move`→`Face`→`Interact`, verbs published by the prop). The *body* does not reach or align to the object | 3, 5 |
| **Turn and look toward dynamic targets** | **works.** `lookAt`/`interest` publish a target; the aim layer turns head, eyes, mouth and antenna, with the entity-scale conversion correct | spine distribution (3) would improve it |
| **Execute high-level goals without a director** | **works.** Five considerers, scored options, dwell and margin, `ActionDesc` output. All five Glowmere aliens run on `decide` | 9 extends the vocabulary |
| **Two characters doing the same thing differ** | **works.** Per-entity PCG32 keyed on seed or FNV-1a(name, sceneSeed); phase-shifted sense/decide cadence | motion variation (7) deepens it |
| **Suitable for realtime AV rendering** | **works.** Rig LOD on a timeline grid, entity cull/coarse bands, budgeted perception | measure `RigStats::cpuMs` |
| **Every advanced subsystem has a deterministic fallback** | **mostly works** — §13's ladder is eight rungs and seven already exist | the provider rung (1c) |

The honest summary is that **six of ten already pass, and the two that fail hardest are both foot
contact**. That is a different project from the one the brief imagines.

---

## 19. The "DO NOT BUILD YET" list

Explicit, so that a reviewer can point at it.

1. **Motion matching of any kind** — gated on ADR-540's two gates.
2. **Learned motion matching** — gated on a database exceeding ~100,000 frames.
3. **Any neural network in the runtime** — there is nothing worth inferring yet.
4. **Any mixture-of-experts / neural state machine / scene-interaction network** — the data cost is
   a mocap shoot with per-frame hand labelling including phase (§4.3), and the licence forbids the
   reference implementation.
5. **Physics, ragdolls, partial ragdolls, dynamic balance** — §6.
6. **Runtime generative or diffusion motion** — §7.
7. **A nearest-neighbour acceleration structure** — §2.4, measured unnecessary.
8. **The early-out search loop** — §2.3, measured 2.0-2.7× slower here.
9. **Threading the entity update** — two subsystems assume single-threadedness by documented
   contract (`nav_grid.hpp:349-351`, `perception.hpp:98-101`).
10. **Threading rig posing** — until `RigStats::cpuMs` is surfaced and shows it matters.
11. **Crowd/hundreds-of-characters work** — the scene runs 21 animated characters; the LOD bands
    exist; nothing is measured as a problem.
12. **An animation impostor / baked-motion LOD rung** — design for it (§9), do not build it.
13. **A re-export of the alien pack** — §12.2(a) is a full-content regression; try (b) first.
14. **Any vendoring of AI4Animation, SAMP, `pau1o-hs` or `E1P3` code** — §4.1, §4.4, §3.5.
15. **Anything at all before the §16 probe has run.**

---

## 20. Appendix: probes used in this document

All under `scratchpad/anim-research/`. None links or includes anything from AV Gen; none touches
the repository. Each is ADR-182-safe (every arm can fail, and the failure is detected).

| probe | question | key result |
|---|---|---|
| `clipstats.py` | how much motion does AV Gen own? | 26 clips, 57.07 s, 267 channels each, 156,921 keys |
| `rootpath.py` | do the locomotion clips travel? | no: `Walking` moves its root 3.2 cm in X over 1.03 s |
| `mmprobe.cpp` | what does a brute-force MM query cost here? | 6.7 ns/frame at D=27; early-out is 2.0-2.7× **slower** |
| `poseprobe.cpp` | what does posing this rig cost, for scale? | 2.97 µs for 89 joints / 267 channels (arithmetic shape, not AV Gen's code — a lower bound) |

`mmprobe.cpp`'s correctness arm plants an exact duplicate of the query at a known row and requires
both search implementations to find it, then re-randomises that row so the timing run is not an
early-out best case. `poseprobe.cpp`'s arm requires sampling at a key to reproduce that key.
