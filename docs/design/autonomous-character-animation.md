# Autonomous character animation and motion synthesis: research and architecture

**Status:** Phase 0 research. **Nothing here is implemented.** This is a proposal for review.
**Branch:** `agent/anim-research`. **ADR range:** 540-559.
**Date:** 2026-09-20. **Machine for every measurement below:** Apple M2 Max, 8P+4E, 64 GB.

Every claim about AV Gen in this document cites a file and a line that was read, and every number
is either a measurement with its probe named or is labelled an estimate (ADR-385). **Three claims I
made during this research were wrong and are recorded as such** — two in §1.9 and one in §2.3a —
because the shape of each mistake is worth more than the tidy version. The third is the most
useful: it was a correct, reproducible measurement from a probe that could fail, and the conclusion
was still wrong, because the fixture was.

---

## 0. The four sentences

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

3. **The data gate is openable and the engineering gate is not — which inverts the expected
   order.** I expected licensing to be the wall; it is not. **100STYLE is CC BY 4.0 and is four
   million frames of stylized locomotion** — starts, stops, turns, styles — roughly 2,300× AV Gen's
   entire corpus, shippable as a derived database *and* trainable on (§8.3). ACCAD is CC BY 3.0 and
   CMU permits embedding in a commercial product. What blocks it is that AV Gen cannot get any of it
   onto a rig.

4. **So build the motion-data foundation and the two small quality fixes that pay for themselves on
   the 57 seconds that already exist** — cross-file clip binding + retargeting, phase-matched
   transitions and inertialization, and a leg the foot solver can actually solve. The smallest
   experiment that would validate or kill the whole architecture is in §16 and takes one throwaway
   tool, no engine changes.

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

Probe: `docs/design/anim-research-probes/clipstats.py` and `rootpath.py`, reading
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

Recorded because the shape matters more than the conclusions. A third, larger one is in §2.3a.

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

Four details that reimplementations usually get wrong, all read out of Holden's source:

* **Normalise per *group*, not per scalar.** Offsets (means) are per-dimension; the **scale is one
  number shared by the whole group**, computed as the mean of the group's per-dimension standard
  deviations, so a 3D foot position stays isotropic and you do not accidentally stretch Z against
  X. The user weight enters as a divisor: `scale = std / weight`. And `assert(std > 0)` — the
  source's own comment is *"Features with no variation can have zero std which is almost always a
  bug."*
* **Everything is in *simulation-bone* space**, not hips space. The simulation bone is a
  procedurally generated extra bone: an upper-spine bone projected to ground level and smoothed
  (Savitzky-Golay, windows 31 and 61), with forward taken from the hips. It becomes bone 0 and is
  the frame every feature is expressed in.
* **Only the trajectory half of the query is synthesised.** The pose half is *copied out of the
  database row for the frame currently playing* and de-normalised — not recomputed from the live
  skeleton. That is a large practical simplification and it is what the LMM paper describes too.
* **Search cadence is contested between the two primary sources, and both are "classical".**
  Clavet (GDC 2016) searches **every frame**, with a post-hoc 0.2 s hysteresis window and a 0.25 s
  blend. Holden searches on a timer (`search_time = 0.1f` → **10 Hz**) *plus* a forced search on
  the **trailing edge** of a rapid input change — when the stick stops moving fast — and at the end
  of a clip.

Continuity is three mechanisms, not one, and production implementations use all three:

1. **Seed the search with the current frame**, so every candidate must strictly beat the incumbent.
2. **A transition cost added to every candidate** but not to the incumbent — a margin. (Holden's
   shipped demo leaves this at 0; the mechanism is there and unused.)
3. **An exclusion window**: skip candidates within ±20 frames (±0.33 s) of the current frame, and
   refuse to return a frame within 20 frames of a clip end, "as this can result in re-triggering
   the search too frequently".

Then the discontinuity is **inertialized** away (§2.8).

### 2.2 The three things that make it work, none of which AV Gen has

1. **A root that travels.** §1.8. Without it there is no `t_t`, no `t_d`, and no `ḣ_t`.
2. **Transition content.** The quality of motion matching is the quality of the database's
   coverage of starts, stops, planted turns, direction changes and speed changes. AV Gen has one
   walk cycle and one run cycle.
3. **Inertialization.** `grep -rni "inertial"` over `src/` and `docs/` is empty. Motion matching
   without inertialization is a cross-fade every 10 frames, which is worse than what the engine
   does today.

### 2.3 What a search actually costs — measured

Probe: `docs/design/anim-research-probes/mmprobe.cpp`, standalone, linking nothing from AV Gen. It is
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
**2.97 µs** (`docs/design/anim-research-probes/poseprobe.cpp`; a faithful reimplementation of the
arithmetic shape, *not* AV Gen's code — treat it as a lower bound). So a 20,000-frame search is
**44× the cost of posing the character it is for**, and a 60,000-frame search at D=51 is ~1 ms, or
0.16 ms/frame amortised at a 6 Hz search rate.

### 2.3a The early-out: I was wrong, and the shape of the error is the useful part

My first conclusion from the table above was that the textbook early-out — abandon a row once its
running cost exceeds the best so far — is **2.0-2.7× slower** on this machine, and that anyone
implementing motion matching should not add it. A parallel measurement on Holden's *real*
`features.bin` (53,500 × 27) found the opposite: 388 µs plain, 343 µs with the early-out, a 12%
**win**.

Both measurements are correct. The difference is the data, and the follow-up probe
(`docs/design/anim-research-probes/mmprobe2.cpp`) isolates it. Same code, same machine, four conditions:

| database | query | plain | early-out | ratio |
|---|---|---:|---:|---:|
| white noise | far / random | 361.5 µs | 804.5 µs | **2.23× slower** |
| white noise | near (a real row + 0.35σ) | 356.5 µs | 486.5 µs | 1.36× slower |
| random walk (motion-like) | far / random | 360.8 µs | 391.2 µs | 1.08× slower |
| **random walk (motion-like)** | **near** | **359.7 µs** | **288.0 µs** | **0.80× — a 20% win** |

`-O3 -march=native` does not change the picture (2.40× / 1.57× / 1.12× / 0.81×).

**The early-out's value is entirely a property of the data's temporal locality and the query's
closeness to it, and my original probe had neither.** A motion database is a smooth trajectory
through feature space and a motion-matching query is always *near* the current frame, so the real
case is the bottom row. My synthetic Gaussian database was the top row, which does not occur.

This is the reason the house rule exists. I had a measurement, it was reproducible, the probe could
fail and did not — and the conclusion was still wrong, because the *fixture* was wrong. A probe
that cannot fail proves nothing (ADR-182); a probe that can fail but tests the wrong distribution
proves something about the wrong thing.

The corrected guidance: **measure the early-out on real extracted features, never on synthetic
ones**, and expect a modest win rather than a large one either way — because the real speed comes
from §2.4.

### 2.4 Acceleration structures: the AABB hierarchy, not a KD-tree

The LMM paper's Appendix B is the best published justification and it argues against the obvious
choice:

> *"Rather than a KD-Tree or clustering-based approach we use a simple axis-aligned bounding-box
> (AABB) based method... We fit axis-aligned bounding boxes to groups of 16 and 64 frames
> consecutively in X... Firstly, as we iterate over the database in order, we have excellent cache
> performance and avoid the random access that can occur using structures such as KD-Trees.
> Secondly, the squared distance to an AABB can be computed as a sum of the squared distance along
> each dimension individually. This allows for an essential form of early-out..."*

Measured on the real 53,500 × 27 database, this machine:

| variant | µs/query |
|---|---:|
| scalar brute force, no early-out | 388 |
| scalar brute force, with early-out | 343 |
| **two-level AABB (16/64 frames), as in `database.h`** | **70** |
| 4-wide auto-vectorised float32 | 155-198 |
| int16-quantised, int32 accumulators, 4-wide | 213 |

**4.9× over scalar-with-early-out, for +15.6% memory (0.86 MiB on 5.51 MiB).** It wins precisely
because it is *brute force with skips*: it keeps the linear layout and the prefetcher and simply
declines to look inside boxes it can prove are too far. A KD-tree at 27-55 dimensions is already
in the regime where expected leaf visits approach linear, and it pays random access for the
privilege — which is why Unreal pairs its `PCAKDTree` mode with PCA and also ships a plain
`Brute Force` mode.

**int16 quantisation was 1.4× *slower* than float32 on NEON**, because of the int16→int32 widening
in the squared difference. On AVX2 with `_mm256_madd_epi16` it would probably invert. It is a
memory tool here, not a speed tool, and that is an Apple-Silicon-specific finding.

**The biggest lever is not a structure at all.** Both the LMM paper (precomputed *ranges*) and
Epic (multiple small databases selected by a Chooser Table) reduce `n` by partitioning on tag,
gait or manoeuvre before any index is considered. Epic's answer to database growth in the Game
Animation Sample is explicitly "many small databases plus **pose warping** to fill in the gaps",
not a better index.

**For AV Gen: do not build any of this yet.** At 1,712 frames a plain search is 11.5 µs. The AABB
hierarchy is ~60 lines and is the right structure *if* the database ever reaches tens of thousands
of frames — but it is 60 lines of determinism surface (traversal order affects tie-breaking) bought
for a saving of nine microseconds.

### 2.5 Two numbers I could not reconcile, stated as such

The LMM paper's Table 3 reports a per-frame cost for classical motion matching. Two independent
extractions of the same PDF produced **90 µs** and **909 µs** for the Locomotion row (89,480
frames, 27 features), and **62 µs** vs **626 µs** for Chair. One reading appends a digit the other
drops; the memory columns agree in both readings and close against `frames × features × 4 bytes`.

I did not resolve it and I am not going to guess. What I can say from this machine: scaling my own
53,500-frame measurements to 89,480 frames gives ~594 µs scalar and ~117 µs with the AABB
hierarchy on an M2 Max. **90 µs on a 2020-era CPU is implausible; 909 µs is plausible.** But the
number that should drive an AV Gen decision is the one measured on AV Gen's hardware at AV Gen's
database size, which is §2.3's table, and it does not depend on resolving this.

### 2.6 The architectural fact worth carrying, whatever else is decided

Parsed from the headers of Holden's shipped `features.bin` and `database.bin`:

| | |
|---|---|
| frames | 53,500 (14.9 min at 60 Hz) |
| `features.bin` | 5.51 MiB — **108 B/frame** (27 × float32) |
| `database.bin` | 61.1 MiB — **~1,196 B/frame** of pose data |
| **ratio** | **the thing you search is 0.9% of the thing you store** |

That separation is why the motionpack in §9.4 has `features.bin` beside `clips.bin` rather than one
blob, and it is why learned motion matching's compression targets the *pose* database (§3).

At AV Gen's actual scale these numbers say motion matching is cheap. They also say the cheapness is
irrelevant, because the database is 57 seconds.

### 2.8 Inertialization — the thing AV Gen is missing and the cheapest to add

`grep -rni "inertial" src/ docs/` is empty. Every transition in AV Gen is a cross-fade, which means
two clips are evaluated for the duration and the cost of a transition scales with how many joints
disagree. Inertialization replaces that with: **capture the pose difference at the moment of the
switch, then decay it to zero as a post-process.** Only the target is evaluated. Fixed cost. Fire
and forget.

There are two formulations and AV Gen should take the second.

**Bollo's quintic** (*Inertialization: High-Performance Animation Transitions in "Gears of War"*,
GDC 2018). Per channel, with `x0` the offset magnitude and `v0` its finite-differenced velocity:

```
if (x0 == 0 || v0 > 0) skip                       // nothing to fix, or already separating
t1 = min(t1_desired, -5 * x0 / v0)                // clamp so the curve cannot be forced to overshoot
a0 = (-8 * v0 * t1 - 20 * x0) / (t1 * t1)         // zero jerk at t1
A  = -( a0*t1*t1 +  6*v0*t1 + 12*x0) / (2*t1^5)
B  =  (3*a0*t1*t1 + 16*v0*t1 + 30*x0) / (2*t1^4)
C  = -(3*a0*t1*t1 + 12*v0*t1 + 20*x0) / (2*t1^3)
x(t) = A t^5 + B t^4 + C t^3 + (a0/2) t^2 + v0 t + x0
```

Two details that are routinely missed: **do not inertialize x, y, z independently** — decompose a
vector into direction and magnitude and inertialize the *magnitude*, because per-component time
clamping produces visible artifacts when the component velocities differ; and for quaternions,
decompose into axis and angle and inertialize the *angle*. And the pose history buffer must hold
the **inertialized output**, not the raw graph output, so back-to-back transitions compose.

**Holden's spring** (`spring.h`, MIT, 214 LOC) is a critically-damped exponential decay of the
offset, parameterised by a half-life rather than a duration:

```cpp
float halflife_to_damping(float h) { return (4.0f * LN2f) / (h + 1e-5f); }
void decay_spring_damper_exact(float& x, float& v, float halflife, float dt) {
    float y = halflife_to_damping(halflife) / 2.0f;
    float j1 = v + x*y;
    float eydt = fast_negexpf(y*dt);
    x = eydt*(x + j1*dt);
    v = eydt*(v - j1*y*dt);
}
void inertialize_transition(vec3& off_x, vec3& off_v, vec3 src_x, vec3 src_v, vec3 dst_x, vec3 dst_v) {
    off_x = (src_x + off_x) - dst_x;   // folds the OLD residual into the new offset
    off_v = (src_v + off_v) - dst_v;
}
```
Default half-life 0.1 s.

**Take the spring.** The quintic reaches exactly zero in finite time and bounds overshoot
explicitly; the spring never exactly reaches zero but retriggers trivially — `inertialize_transition`
folds the old residual into the new offset, so a system that retriggers every ~10 frames needs no
bookkeeping at all. More importantly for AV Gen: **the spring is a closed form in
`(offset, offset velocity, half-life, elapsed)`**, which fits the engine's "store *when*, not how
long" rule (`src/scene/animation.hpp:8-16`) exactly. It is state, so by ADR-541 it lives in
`MotionMemory` in the entity tier — but it is three floats per channel and a pure function of them.

A related and genuinely elegant idea worth recording: Holden's *Inertialization Transition Cost*
defines the transition cost as **the total displacement the inertialized transition will cause**,
which for a spring integrates to `(2·x·y + v)/y²` and reformulates as a **precomputable per-frame
feature** — `(2·pos/y) + (vel/y²)`, with cost `|source − destination|`. It answers "how do I weight
position against velocity" by deriving the weight from the blend you were going to do anyway.

### 2.9 Reference implementations, assessed

| repo | license | size | verdict |
|---|---|---|---|
| **[orangeduck/Motion-Matching](https://github.com/orangeduck/Motion-Matching)** | **MIT**, © 2021 Daniel Holden, real LICENSE | 4,742 LOC C++ (`controller.cpp` 2,486; `database.h` 732; `lmm.h` 257; **`nnet.h` 173**; `spring.h` 214) + 2,355 LOC Python training | **The only artifact in this study that could be vendored.** By the paper's first author, last pushed 2025-02-06. Both classical and learned MM. raylib is the demo window only. |
| **[orangeduck/Spring-It-On](https://github.com/orangeduck/Spring-It-On)** | **MIT**, © 2021 Daniel Holden | small | The damper/spring reference. This is where the inertialization arithmetic comes from. |
| [SaxonRah/OpenMotion](https://github.com/SaxonRah/OpenMotion) | **NONE.** `license: null`, no LICENSE file in 1,004 files. One in-file comment says "MIT License" with no license text — **all rights reserved by default** | `MotionMatchingComponent.cpp` is **10.2 KB / 310 lines**; the repo's real content is FABRIK IK (`FabrikChain.cpp` 47.8 KB) | **A FABRIK IK plugin with a motion-matching sketch bolted on**, 6 commits total. The author's own README: *"I don't remember how much of the FK, Physical Anim, and Motion Matching systems were implemented, and I don't remember what was broken/needs fixes."* Its matcher has no character-space rotation removal, **no normalisation at all** (it sums squared distances and `1 − dot` in one cost), no early-out, no continuity bias, no transition cost, no current-frame seeding, no inertialization — and its query sets *the same* placeholder velocity for all five bones (`// uniform approx`). **Do not vendor. Read as a 310-line illustration if you like.** |
| [aaron1a12/wm-motion-matcher](https://github.com/aaron1a12/wm-motion-matcher) | **MIT** | 187 KB / 39 files; `AnimNode_MotionMatcher.cpp` 1,554 LOC | 4 commits in 38 minutes in 2022, untouched since. Author's README: *"It's not tested in its current form... and might not even compile in Unreal."* **The best-informed design of the three UE repos**: proper per-feature running std with a precomputed inverse, and `TrajectoryTimings = {-0.25, 0.25, 0.5, 0.75, 1.0}` — note the **past sample at −0.25 s**, a real technique for penalising reversals that Holden's 27-dim layout omits. Worth reading for the schema. Expect it not to build. |
| [KamatMayur/UE5_MotionMatching](https://github.com/KamatMayur/UE5_MotionMatching) | MIT | **15 files, 16,147 bytes total** of C++ | **There is no implementation in it.** `MotionSynthesis.h` (2.3 KB) is empty `UDataAsset` declarations; the rest is module boilerplate and the stock UE5 third-person character. The 435 MB is Manny/Quinn assets. Ignore. |

**The data is the catch, not the code.** Holden's repo builds its database from Ubisoft **LAFAN1**,
which its own README says is **CC BY-NC-ND 4.0** — "unlike the code, which is licensed under MIT".
So the algorithm is free and the reference motion is not, and `resources/*.bin` must not ship.

One correction to the brief: **there is no int16-quantised database in Holden's repo.** Features and
poses are float32 throughout, and the README says outright that it "does not contain some of the
briefly mentioned optimizations to the animation database storage". Quantisation in this literature
traces to Büttner (i3D 2019), who mapped features into a quantised space with product quantisation
(Jégou et al. 2011).

### 2.10 What Unreal shipped, and the one lesson worth stealing

UE5's Pose Search plugin uses three assets — a **Schema** (what to match on), one or more
**Databases**, and the **Motion Matching** anim-graph node — with a **Chooser Table** selecting
*which database* to search from gameplay context. A new Schema arrives preconfigured with a
Trajectory channel and a Pose channel, and the Pose channel defaults to `foot_l` and `foot_r`.
Same schema as Clavet's and Holden's: feet plus trajectory.

Its continuity settings are named versions of §2.1's three mechanisms: `Continuing Pose Cost Bias`
(the margin), `Pose Jump Threshold Time` (the exclusion window), `Pose Reselect History`,
`Search Throttle Time` (the cadence). Search modes are `Brute Force`, `PCAKDTree` and an
experimental `VPTree` — note that Epic, like the LMM paper, only offers a KD-tree *paired with*
PCA. **Epic publishes no millisecond or memory figures anywhere**; the guidance is "profile with
Unreal Insights and partition your databases."

**The lesson:** Epic's answer to database growth is not a better index. It is **many small
databases selected by a decision table, plus pose warping to fill in what is not in the data**.
AV Gen already owns the decision table — that is what `Selector` and the considerer list are
(ADR-333) — and "pose warping" is what the IK/procedural layer is for. Both halves of Epic's
scalability answer map onto systems this engine already has.

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
| Locomotion | 89,480 | 52.1 MB | 5.3 MB | 9.8× | 90 or 909 † | 197 |
| Terrain | 170,534 | 104.7 MB | 5.5 MB | 19× | 213 † | 200 |
| Dog | 124,418 | 136.5 MB | 6.5 MB | 21× | 111 † | 262 |
| **Bear** | 694,272 | 995.6 MB | 7.1 MB | **140×** | 946 † | 340 |

† **The memory columns are solid and the MM timing column is not.** Two independent extractions of
this PDF disagreed by a factor of ten on the classical-MM timings (§2.5); the memory figures agree
in both readings and close against `frames × features × 4 bytes`. The LMM timings are confirmed by
their own arithmetic (Locomotion: Decompressor 85 + Stepper 100 + Projector 127 run every 10th
frame = 85 + 100 + 12.7 = 197.7 ✓). **Do not cite the MM µs column without re-reading the paper.**

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

## 6a. Multiple characters, and the wall nobody has hit yet (Part 18)

`Composition` **deep-copies the whole rig per node instance**, on purpose:

> *"ADR-086: rigs are copied per node instance, not per asset. Two nodes on the same character file
> are two characters, and they must be able to be doing different things; sharing one pose between
> them is the bug, not the saving."*
> — `src/scene/composition.cpp:4873-4878`, and the copy itself is `SkinnedRig rig = src;` at `:4879`

The pose must be per-instance. The *clips* need not be, and today they are: each copy carries all
26 animations. Measured from the file — 156,921 keys across 6,942 channels — each channel stores a
`float` time and a `glm::vec4` value per key, so one alien's clip data is

```
156,921 × (4 + 16) B  ≈  3.14 MB   + ~0.33 MB of vector headers  ≈  3.5 MB per character instance
```

| characters | clip memory |
|---:|---:|
| 5 (today's Glowmere cast) | ~17.5 MB |
| 50 | ~175 MB |
| 200 | ~700 MB |

`assets/aliens/ATTRIBUTION.md` already records the disk half of this (3.9 MB of each 4.5 MB file is
animation) and attributes it to the same cause: there is no way to bind one file's clips to another
file's skeleton (§1.5).

**This is the real Part 18 finding, and it is not about LOD.** The LOD machinery already exists and
is good: rig pose rate on a timeline grid with near/far/cull bands, entity cull and coarse bands,
phase-shifted perception and decision cadences. What does not exist is **clip sharing**, and a
motionpack fixes it for free — a pack is immutable, read-only, shared by every instance, and
referenced rather than copied. `SkinnedRig` keeps its `Pose`, `palette`, `player` and `layers`;
`clips` becomes a `const MotionPack*`.

That is worth naming as a *second* reason to build the motionpack, independent of motion matching:
**it is the fix for character memory scaling, and it is the only one.**

Design for the impostor rung the brief describes (hero = full intelligence, background = cheap
procedural, distant = baked) and do not build it. Nothing in the 21-character scene is measured as
a problem, and the first measurement to take is `RigStats::cpuMs`, which already exists and has no
reader.

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
characteristic failures are exactly the things an offline pipeline can measure and an artist cannot
be asked to eyeball across hundreds of clips. Foot-slide per frame, penetration depth, and
joint-velocity spikes are all computable.

How bad the raw output is, for scale: measured on HumanML3D, **raw MDM floats 18.9 mm above the
floor, penetrates 11.3 mm into it, and skates on roughly one frame in ten**. That is a rebuild, not
a polish pass. And the practical blocker for automating the cleanup is sharp: **IK foot-locking
needs better than 95% foot-contact prediction accuracy, and typical accuracy on complex motion is
85-90%** — which is why a model that *ships its own per-frame contacts* is worth more here than a
model with a better FID.

None of these models knows terrain exists. Every one generates a lone body on a flat infinite floor
at y = 0, so every clip would have to be re-grounded against AV Gen's actual terrain. And the
stylistic ceiling is real for this project specifically: a human-mocap-trained model *"cannot
generate cartoon or non-physically plausible motions"*, and the Glowmere cast is a stylized
non-human biped.

**The licensing answer is in §8.5 and it is decisive**: the entire MDM/MotionDiffuse/MLD/T2M-GPT/
MoMask family is trained through HumanML3D to AMASS, which forbids commercial training outright, so
their output is not shippable. There appears to be one exit — NVIDIA's 2026 Kimodo and ARDY,
Apache-2.0 code on commercially-cleared mocap — and it is CUDA-only and unverified beyond the repo
metadata. **Treat generative motion as a research item with a named revisit condition, not as a
phase.**

---

## 8. Licensing, which decides more of this than the engineering does

### 8.1 The rule

Six categories must be tracked separately, and conflating them is how a research asset ships:

| category | example | governs |
|---|---|---|
| **runtime code** | a motion-matching search | what AV Gen links |
| **offline/training code** | a PyTorch training script | a build-time tool, like a shader compiler |
| **raw mocap data** | LAFAN1's BVH files | what an artist may import |
| **a derived motion database** | a `.motionpack` built from that mocap | **what AV Gen ships** |
| **trained model weights** | a Decompressor `.bin` | **what AV Gen ships** |
| **generated motion** | a diffusion model's output | **what AV Gen ships** |

The last three are where the trap is. A permissively-licensed *algorithm* trained on or derived
from a non-commercial *dataset* produces a non-commercial *artifact*, and several of the licences
below say so explicitly rather than leaving it to inference.

### 8.2 What was verified, with the text

| thing | licence | verified? | may AV Gen ship a derived database? |
|---|---|---|---|
| **AV Gen's own alien pack** | **CC0 1.0** (owner's statement, `assets/aliens/ATTRIBUTION.md`) | read in repo | **yes** |
| **AV Gen's farm pack** | same family, per the same statement | read in repo | **yes** |
| `orangeduck/Motion-Matching` **code** | **MIT**, © 2021 Daniel Holden, real LICENSE file | read | **yes** |
| `orangeduck/Spring-It-On` code | **MIT**, © 2021 Daniel Holden | read | **yes** |
| **LAFAN1** (Holden's reference data, and `E1P3`'s) | **CC BY-NC-ND 4.0** — the repo's own README: *"The data required if you want to regenerate the animation database is from this dataset which is licensed under Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International Public License (unlike the code, which is licensed under MIT)."* | read | **no.** NonCommercial *and* **NoDerivatives** — a retargeted database is a derivative |
| **AI4Animation** (all of it, 2017-2024) | **no LICENSE file.** README only: *"This project is only for research or education purposes, and not freely available for commercial use or redistribution. The motion capture data is available only under the terms of the Attribution-NonCommercial 4.0 International (CC BY-NC 4.0) license."* | read, 404 on the licence API | **no** |
| **AI4AnimationPy** (2026 remake) | **CC BY-NC 4.0 applied to the code itself** | LICENSE read | **no** |
| **SAMP code** | AI4Animation's terms **plus**: *"You may use, reproduce, modify, and display the research materials... solely for noncommercial purposes... **You may not redistribute the Research Materials.**"* | read | **no**, and it cannot even be redistributed |
| **SAMP dataset** (Max-Planck) | *"Any other use, in particular, any use for commercial... purposes is prohibited... This license also prohibits the use of the Dataset to train methods/algorithms/neural networks/etc. for commercial... use... **No Distribution** — ... shall not be copied, shared, distributed... except that you may make one copy for archive purposes only."* | read | **no**, and the restriction is explicitly **viral onto trained weights** |
| `pau1o-hs/Learned-Motion-Matching` | **no LICENSE file** — default copyright | API | **no** |
| `E1P3/Learned_Motion_Matching_UE5` | **no LICENSE file** | API | **no** |
| `E1P3/Learned_Motion_Matching_Training` | **MIT**, © 2024 | API | yes (the code) |
| `SaxonRah/OpenMotion` | **no LICENSE file** in 1,004 files; one in-file "MIT License" comment with no licence text → all rights reserved | API + tree | **no** |
| `aaron1a12/wm-motion-matcher` | **MIT** | API | yes |
| `KamatMayur/UE5_MotionMatching` | MIT (but there is nothing in it) | API | n/a |

### 8.3 The shippable list — this is the most valuable finding in the report

A motion corpus AV Gen may legally ship a derived database of **does exist**, and it is large.

| source | licence, verbatim where quoted | commercial? | ship a **derived** database? | train on it? |
|---|---|---|---|---|
| **100STYLE** — <https://zenodo.org/records/8127870> | **CC BY 4.0** (Zenodo API: `"license": {"id": "cc-by-4.0"}`). *"over four million frames of stylized motion capture data"*, BVH + processed, 100 styles | **yes** | **yes** | **yes** |
| **ACCAD Open Motion Project** (Ohio State) | *"Open Motion Project by ACCAD/The Ohio State University is licensed under a Creative Commons Attribution 3.0 Unported License."* ~300 BVH across female1/male1/male2, martial arts, running, walking | **yes** | **yes** | **yes** |
| **CMU Motion Capture Database** | *"This dataset of motions is free for all uses."* … *"You may include this data in commercially-sold products, but you may not resell this data directly, **even in converted form**."* | **yes** | **no if the database is the product**; yes embedded in AV Gen | not addressed |
| **Kenney.nl** (already in `assets/kenney`) | **CC0.** *"Attribution is not required"* | yes | yes | yes |
| **Mixamo (Adobe)** | Adobe: *"You can use both characters and animations royalty free for personal, commercial, and non-profit projects"*. Adobe General Terms §3.6: *"under no circumstances can you distribute the Content Files on a stand-alone basis, outside of the End Use."* §17: must not *"create, train, test, or otherwise improve any machine learning algorithms"* | **yes, embedded** | **no** — a retargeted pack is stand-alone | **no** |
| **Quaternius** (already in `assets/quaternius`) | QAL v1.0: *"You just can't resell or redistribute the assets themselves as assets"*; §3(a): *"This restriction applies regardless of how much the Assets have been modified"* | yes, embedded | **no** | not addressed |
| **AV Gen's own alien + farm packs** | **CC0 1.0** (owner's statement, `assets/aliens/ATTRIBUTION.md`) | yes | **yes** | yes |

**Two traps worth naming explicitly.**

1. **You cannot launder permissiveness out of AMASS.** AMASS unifies 15 datasets and the MPI umbrella
   is the *floor*, not the ceiling — **CMU data inside AMASS is more restricted than CMU data from
   CMU**. Same for ACCAD. If you want CMU or ACCAD commercially, go to the original site.
2. **An MIT `LICENSE` on a motion-dataset repo almost always covers the processing scripts, not the
   motion.** HumanML3D is MIT and *contains no motion data*: *"Due to the distribution policy of
   AMASS dataset, we are not allowed to distribute the data directly."*

### 8.4 The cannot-ship list

**AMASS** (*"Any other use, in particular any use for commercial purposes, is prohibited… This
license also prohibits the use of the Dataset to train methods/algorithms/neural networks/etc. for
commercial use of any kind"*, plus a No-Distribution clause), **SMPL / SMPL-H / SMPL-X** (same MPI
terms, *and patented* — US10395411B2 — and you cannot evaluate an AMASS pose without it),
**LAFAN1** (CC BY-NC-**ND**: *"you do not have permission under this Public License to Share
Adapted Material"* — a retargeted database is the textbook Adapted Material), **Human3.6M**
(*"GRANT OF LICENSE FREE OF CHARGE FOR ACADEMIC USE ONLY"*), **KIT-ML** (no licence text exists
anywhere — unlicensed is not permissive), **HumanML3D**, **HumanAct12/PHSPD**, **AIST/AIST++**,
**Motorica**, **MoVi/BML**, **SFU**, **Bandai-Namco**, **Motion-X**, **MotionMillion**,
**Nymeria**, **SnapMoGen** — all non-commercial, all out.

Plus, from §4: **AI4Animation** (no licence file at all), **AI4AnimationPy** (CC BY-NC on the code),
**SAMP** (*"You may not redistribute the Research Materials"*), and **SAMP's dataset** (viral onto
trained weights).

### 8.5 Generative motion: the chain, and the one exit

**MDM, MotionDiffuse, MLD, T2M-GPT, MoMask, MotionGPT, MotionLCM, StableMoFusion, MotionCLR and
MotionStreamer are all trained on HumanML3D and/or KIT-ML. HumanML3D derives from AMASS and is
expressed in SMPL. AMASS forbids training for commercial use of any kind.** The MIT and Apache
labels on those repos cover the code, not the weights and not the data. **The output of that entire
family is not shippable.** That closes §7's generative-offline architecture for the models most
people would reach for.

There appears to be an exit, and it should be verified rather than trusted: two 2026 NVIDIA
releases, **Kimodo** (`nv-tlabs/kimodo`, Apache-2.0 code, 282 M params, 2-5 s/clip) and **ARDY**
(`nv-tlabs/ardy`, Apache-2.0, 156 M params, 33-63 ms/clip), are trained on **commercially-licensed
optical mocap with stated IP clearance — no AMASS, no HumanML3D, no KIT-ML** — and their weights
carry NVIDIA's Open Model licence (*"ready for commercial use"*, *"NVIDIA does not claim ownership
to any outputs"*).

**The detail that makes this credible is a negative one**: Kimodo's *SMPL-X* variant is released
under a different, internal-research-only licence, while its SOMA and G1 variants are open. That is
a well-resourced legal department declining to ship an SMPL-derived artifact commercially, and it
is the strongest independent evidence that the AMASS/SMPL chain is real.

Both are **CUDA-only**. Neither knows your terrain exists (*"Not aware of objects in the scene around
a character"*), and both skate above ground truth (Kimodo 3.87 cm/s vs 2.21 cm/s GT).

**Quality, for scale.** Raw MDM on HumanML3D floats **18.9 mm** off the floor, penetrates **11.3 mm**
into it, and skates on roughly **1 frame in 10**. That is not a polish pass; it is a rebuild. The
practical blocker for automated cleanup is sharp and worth recording: **IK foot-locking needs >95%
contact-prediction accuracy and typical accuracy on complex motion is 85-90%** — which is exactly
why Kimodo's 0.98 contact accuracy and ARDY's shipping per-frame contacts in its output matter more
than either model's FID.

### 8.4 The mechanism, not the policy

A rule that lives in a document gets broken. Two enforcement points:

1. **`pack.json` carries a required `license` field and the build fails without it.** Not a
   warning — this codebase has a standing lesson that an unreported no-op is the failure
   (`src/scene/pose_layers.hpp:255-259`).
2. **The pack records its provenance**: every source file, its licence, and the transform chain
   applied. A derived database whose ancestry cannot be printed is a database nobody can clear.

### 8.6 The practical consequence, which is better than expected

The content gate on ADR-540 is **openable**. **100STYLE alone is four million frames of stylized
locomotion under CC BY 4.0** — roughly 2,300× AV Gen's entire current corpus, with the starts,
stops, turns and style variation that §1.8 says are missing, and with a licence that permits a
shipped derived database *and* training. ACCAD adds ~300 BVH under CC BY 3.0. CMU adds a very large
corpus that may be embedded commercially but not resold "even in converted form" — which AV Gen
satisfies, since the pack is part of the application rather than the product.

So the sequencing is: **retargeting (§12.1) is the gate, not the data.** Get 100STYLE onto the alien
rig and the motion-matching question becomes a real one. Until then it is not.

Two things that are *not* solved by that: the alien is a stylized non-human biped at 1.94× scale,
and human mocap retargeted onto it may fight the silhouette (Kimodo's card: *"Cannot generate
cartoon or non-physically plausible motions"*); and nothing in any licensed corpus is Glowmere's
motion. Owning travelling locomotion authored on the alien rig remains the highest-fidelity answer
and is an art task.

---

## 8a. Audio-reactive animation (Part 19)

**The seam already exists and is already used.** `BehaviorContext` carries the signal bus and hands
behaviours two accessors — `signal(name)` for a continuous value and `event(name)` for a discrete
one (`src/entity/behavior.hpp:140-141`). Three behaviours already consume it:

* `spin` pushes its yaw rate on a named signal (`src/entity/behaviors.cpp:532`);
* `interest` raises `reaction` from a named signal above a threshold, with a cooldown
  (`src/entity/behaviors.cpp:943-944`) — and `reaction` is what drives the additive `startle` layer
  on all five Glowmere aliens;
* `explore` triggers a hop on `jumpSignal` (`src/entity/behaviors.cpp:1558-1559`).

So "beat → motion accent" is already authorable today, in JSON, with no engine change.

**The rule this architecture must preserve: audio reaches animation through the parameter and
behaviour layers, never through the motion provider.** A provider that read the signal bus would be
a provider whose output depends on something outside `(request, memory)`, which breaks ADR-541 and
with it every determinism guarantee below it. What audio may do is:

1. move a **parameter** that a considerer scores against (music energy → a `interest` weight, so a
   character is more curious in a chorus);
2. move a **field** of `MotionRequest` (`urgency`, `energy`, `caution`) — which is precisely why
   those three fields are in §9.3 rather than being a style enum;
3. move a **pose layer weight** directly, which it already does through `reaction`.

All three go *into* the request. None of them reaches past it.

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

## 10. Apple Silicon: the neural runtime question, measured and closed

The brief asks whether to use ONNX Runtime, Core ML, MPSGraph or something else if AV Gen ever runs
a network. **The answer is none of them**, and it is settled by measurement on this machine rather
than by preference.

### 10.1 The measurements

Apple M2 Max, batch 1, steady state, per inference. Target shape is the LMM family: 3-6 dense
layers, 512 wide, 1-4 MB of fp32 weights, 1-4 evaluations per frame.

| network | params | hand-written C | Accelerate `cblas_sgemv` | ONNX Runtime 1.30 CPU | ORT + CoreML EP |
|---|---:|---:|---:|---:|---:|
| 64→512→400 | 238 K / 0.91 MB | 13.4 µs | **6.4 µs** | 33.5 µs | 257 µs |
| **64→512→512→400** (the Decompressor's shape) | 501 K / 1.91 MB | **23.9 µs** | **10.4 µs** | 41-98 µs | 381 µs |
| 64→512×4→400 | 1.03 M / 3.92 MB | 51.4 µs | 17.5 µs | 184 µs | 545 µs |
| 256→1024×3→512 | 2.89 M / 11.0 MB | 135.6 µs | 48.1 µs | — | — |

Cache-cold (evicting the weights between inferences) costs about 2.2× — 22.8 µs vs 10.4 µs — so
budget **10-25 µs per network per frame**, not 10.

### 10.2 Why every offload path loses

* **ANE / Core ML.** There is a **fixed dispatch floor of ~0.23 ms per evaluation on Apple Silicon,
  independent of the work inside it** — a 64-element linear costs the same as a small convolution,
  and ~98% of the wall time is host and firmware dispatch rather than engine compute. That is
  **22-37× the entire CPU inference cost** for zero arithmetic. ANE is a low-latency device for
  encoders at moderate batch; it is the wrong device for one 400-wide vector.
* **Any GPU path (MPSGraph, Metal, Core ML on GPU).** Measured directly: a *trivial* Metal compute
  kernel with synchronous `commit` + `waitUntilCompleted` costs **275-278 µs**, and **four kernels
  in one command buffer cost the same as one** (195-274 µs). The round-trip is pure dispatch
  latency and is completely insensitive to the work inside it. The only escape is fire-and-forget
  with the result consumed next frame — which buys a frame of latency in a system whose whole point
  is same-frame pose evaluation, and destroys deterministic frame ordering.
* **ONNX Runtime.** MIT and clean, but the macOS arm64 dylib is **41.8 MiB**, it is 4-10× slower
  than Accelerate on this shape, and — decisively — **it makes no reproducibility promise**. The
  maintainers' position on record is that results "may vary within a certain tolerance level", with
  1e-5 given as an example. Its CoreML EP silently demotes to fp16.

AV Gen has exactly one precedent for a prebuilt ML-adjacent binary — OpenImageDenoise, whose network
runs through BNNS/Accelerate — and ADR-353 records what it cost: a 51 MB download, `OFF` by default,
and an inability to build from source without oneTBB and a third-party compiler toolchain. That ADR
is both the precedent and the warning.

### 10.3 Determinism, which is the actual deciding factor

Measured on this machine, hashing a fixed input's 400-float output:

| condition | result |
|---|---|
| run-to-run, same binary (hand-written **and** Accelerate) | **bit-identical over 1,000 runs** |
| hand-written vs Accelerate, same input | **383 of 400 outputs differ in bits** (max abs 5.6e-8) |
| `-O0` … `-O3` … `-Os`, ±vectorisation, `-mcpu=apple-m1`/`m2`, **all with `-ffp-contract=off`** | **one identical hash across every combination** |
| `-O2` / `-O3` at clang's **default** | a *different* hash |
| `-ffast-math` | a third hash |

**Apple clang contracts to FMA by default**, which silently changes the bits between a debug and a
release build. With **`-ffp-contract=off`** the result is bit-identical across optimisation level,
vectorisation and `-mcpu` target. That is the strongest determinism guarantee available on this
platform and it costs one flag.

The delta from contraction is about 1 ulp. That is harmless in a feed-forward pass and it is *not*
harmless in the Stepper, which **feeds its own output back in every frame**.

**Accelerate is not safe cross-machine.** It dispatches different kernels per microarchitecture
(NEON vs AMX vs SME), and a kernel change is a summation-order change. If an M1 and an M4 must
produce the same bits, only the hand-written path gets there.

### 10.4 The decision

1. **A hand-written forward pass**, ~90 lines, `restrict`-qualified, with the ReLU zero-skip.
   Holden's `nnet.h` is exactly this: **173 lines total, 123 non-blank, 86 for the forward pass**,
   MIT, and it shipped in a AAA production at 197 µs/frame for three 512-wide networks on 2020
   hardware. This machine is ~8× faster than that hardware.
2. **`-ffp-contract=off` pinned in CMake on the inference translation unit specifically**, with a
   golden-vector test that hashes a fixed input's output. That test is how a toolchain upgrade
   announces that it moved the bits.
3. **An optional `cblas_sgemv` fast path** behind a flag, cross-checked against the reference with a
   stated tolerance — never mixed at runtime, because the two are *different implementations*, not
   different optimisations.
4. **No ONNX Runtime, no Core ML, no ANE, no GPU, ever, for this workload.**
5. **Own the model file format.** Use ONNX only as the export path out of PyTorch and convert to a
   flat versioned blob at bake time. Holden's own format is the cautionary tale: `nnet_load` reads
   means, stds, a layer count and the arrays, with **no magic number, no version, no architecture
   hash and no shape assertion** — feed it a file from a different training run and it produces
   plausible garbage, silently. A model header must carry: magic + format version; an architecture
   hash; the normalisation statistics (they *are* part of the model); the **feature schema** (bone
   order, joint count, layout, latent dim, rotation convention, units); the **timestep assumption**
   (`dt = 1/60` is baked into the velocity terms); a training-data provenance ID (§8); and the
   numeric contract plus a golden hash.

**Budget: 10-25 µs per network per frame; four networks ≈ 0.4-1% of a 60 Hz frame.** The network is
free. Everything expensive in this problem is framework and dispatch.

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
| **Classical motion matching** | retargeting, a real dataset, inertialization | §2. The algorithm is easy; the data was assumed impossible and is not — **100STYLE is CC BY 4.0 and is 4 M frames of stylized locomotion** (§8.3). Retargeting is the gate, not the licence |
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
| **ONNX Runtime, Core ML, the ANE, or any GPU path for these networks** | §10, measured on this machine: 41.8 MiB and 4-10× slower for ORT; a 0.23 ms dispatch floor for the ANE; a 275 µs Metal round-trip insensitive to its own payload; and no reproducibility promise from either ORT or Core ML |
| **Any model from the MDM / MotionDiffuse / MLD / T2M-GPT / MoMask family** | §8.5. All trained on HumanML3D → AMASS, which forbids commercial training outright. Their MIT and Apache labels cover the code |
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
| **6** | Classical motion matching as `MatchingProvider`, **gated on 100STYLE (or equivalent) having been retargeted onto the alien rig with measured contacts and trajectories**. Two-level AABB only if the database exceeds ~20k frames. | 2, 1a, 4 | 7 |
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
7. **Prints all 89 joints with their rest-pose model-space positions and their parent**, and
   attempts a **humanoid role map** — hips, spine, neck, head, upper/lower arm, hand, upper/lower
   leg, foot, toe — against them, reporting which roles it can and cannot fill. This is the first
   concrete step of §12.1 and the cheapest possible read on whether retargeting onto this rig is a
   mapping problem or a rig problem.

### What it proves or kills

| result | conclusion |
|---|---|
| Contacts are clean and phases are stable | Phase-matched transitions and contact-weighted foot IK are buildable now. This is the load-bearing claim of phase 1a |
| Contacts are noisy or ambiguous | Every downstream feature needs authored contacts, and the offline pipeline grows a manual stage. **Price phase 1 higher.** |
| Non-ancestor two-bone solve lands the tip within a millimetre | §12.2(b) is the answer and the alien can have feet without an asset re-export. **This is the single highest-value bit.** |
| It cannot | The alien needs a re-export (§12.2a) and the whole contact half of the roadmap is gated on an asset pipeline change with a full-content regression |
| The 1,712-frame feature database gives plausible nearest neighbours for a synthetic query | Motion matching's *machinery* is sound; only the data is missing, and the roadmap is right |
| It does not — the matches are arbitrary because the synthesised trajectories are degenerate | Confirms §1.8 decisively: **motion matching is not reachable from in-place clips, at any effort**, and phases 6 and 8 should be cut until there is travelling content |
| Every humanoid role fills unambiguously from the rest pose | Retargeting 100STYLE onto this rig is a mapping table plus bind-pose conjugation plus bone-length scaling — the textbook pass of §12.1. Phase 2 is priceable |
| Roles are ambiguous or unfillable (three separate leg branches, 18 joints hanging off the armature, `leg_stretch` vs `thigh_stretch`) | The retarget needs a **hand-authored** map per rig pair and cannot be inferred, and §12.1 item 4 ("a rig whose joints are not a hierarchy") is the dominant cost rather than a footnote |

**Why this is the right probe** (ADR-182): every arm can fail, and four of the eight outcomes would
change the plan. It runs on the CPU with no GPU lock (`avgen_tests` territory, never the lock), it
costs no engine risk, and its output is a handful of numbers a human can argue with.

**What it must NOT do:** touch `src/scene/animation.*`, `src/scene/pose_layers.*`,
`src/entity/*`, or any scene file. It reads assets and prints.

---

## 17. Risks

| risk | severity | what it looks like | mitigation |
|---|---|---|---|
| **Content, not code, is the binding constraint** | **medium** (was high) | Motion matching is built and looks worse than the gait machine, because 57 s is not a database | §16 tests it before the build. **100STYLE (CC BY 4.0, 4 M frames) closes the licensing half**; retargeting is what remains |
| **The alien rig cannot be given feet** | **high** | §12.2 both options are expensive; the source `.blend` is not in the repo | §16 arm 6 answers it in a day |
| **Licensing contaminates the deliverable** | **high** | A shipped motionpack derived from AMASS, LAFAN1, Mixamo, or any model trained on HumanML3D. **The trap is that the repos are MIT and the data is not** | Mandatory `license` + provenance in `pack.json`, a build that **fails** without it (ADR-542). Never take CMU or ACCAD *via AMASS* — go to the original site (§8.3) |
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
3. **Any neural network in the runtime** — there is nothing worth inferring yet. When there is,
   §10 already decided the runtime (hand-written matmul, `-ffp-contract=off`), so this is a build
   decision and not a research one.
4. **Any mixture-of-experts / neural state machine / scene-interaction network** — the data cost is
   a mocap shoot with per-frame hand labelling including phase (§4.3), and the licence forbids the
   reference implementation.
5. **Physics, ragdolls, partial ragdolls, dynamic balance** — §6.
6. **Runtime generative or diffusion motion** — §7.
7. **A nearest-neighbour acceleration structure** — §2.4. The two-level AABB is the *right*
   structure and is measured at 4.9× on real data, but it buys nine microseconds at AV Gen's
   database size and costs determinism surface in tie-breaking. Build it when the database reaches
   tens of thousands of frames, and build the AABB, not a KD-tree.
8. **A KD-tree, ever, un-paired with PCA** — §2.4. Both the LMM paper and Epic reached the same
   conclusion independently.
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

All committed under **`docs/design/anim-research-probes/`**, with a README giving the exact commands.
None links or includes anything from AV Gen and none is in the build; they read assets and print.
Each is ADR-182-safe (every arm can fail, and the failure is detected).

| probe | question | key result |
|---|---|---|
| `clipstats.py` | how much motion does AV Gen own? | 26 clips, 57.07 s, 267 channels each, 156,921 keys |
| `rootpath.py` | do the locomotion clips travel? | no: `Walking` moves its root 3.2 cm in X over 1.03 s |
| `mmprobe.cpp` | what does a brute-force MM query cost here? | 6.7 ns/frame at D=27; early-out is 2.0-2.7× **slower** |
| `mmprobe2.cpp` | does the early-out win or lose, and on what? | it depends entirely on the fixture: 2.2× slower on white noise with a far query, **0.80× (a 20% win)** on motion-like data with a near query. §2.3a |
| `poseprobe.cpp` | what does posing this rig cost, for scale? | 2.97 µs for 89 joints / 267 channels (arithmetic shape, not AV Gen's code — a lower bound) |

`mmprobe.cpp`'s correctness arm plants an exact duplicate of the query at a known row and requires
both search implementations to find it, then re-randomises that row so the timing run is not an
early-out best case. `poseprobe.cpp`'s arm requires sampling at a key to reproduce that key.

**And `mmprobe.cpp` still produced a wrong conclusion**, because a correct probe on the wrong
fixture is a correct measurement of the wrong thing (§2.3a). `mmprobe2.cpp` exists because the
finding was contradicted by a second measurement on real data, and the contradiction was worth more
than either number.

---

## 20a. Phase A implementation log

Phase 0 ended with this document. Phase A is the implementation, and this section records what has
landed and what each step *changed about the plan*, because two of the first three did.

### Step 1 — arbitrary IK chains · **DONE** (ADR-543, Accepted)

`PoseLayerStack::bind` no longer refuses a foot chain whose joints are not each other's ancestors.
It records the linkage instead, and `apply` works out per joint which model-space transform that
joint's *parent* has undergone rather than assuming it.

The implementation is smaller than the proposal in §12.2 expected. §12.2 described relaxing the
ancestor check and fixing one lambda; what it did not see is that **the shipped code was already
the general rule with its answers hardcoded**. On an ancestor chain the knee's parent is under the
hip and the tip's parent is under the knee, so the general predicate returns exactly what the old
code assumed. The change is therefore not a special case bolted on — it is the removal of one.

One thing §12.2 missed entirely: **the tip's local was only written when `footAlign > 0`**. On a
nested rig that omission is invisible, because the tip's parent receives the same rigid
pre-multiply the tip does and the recovered local is unchanged. On a detached chain it is the
whole difference between a solve and a no-op. The tip is now written unconditionally.

### Step 2 — body compensation · **DONE** (ADR-544, Accepted)

Promoted ahead of velocity, contacts and phase, because the §9 probe measured the alien's leg at
**98.5% extension with 0.0098 of slack** and that makes foot IK upward-only without it. §5.2 of
this document ranked hip adjustment sixth in "what is missing"; that ranking was wrong and the
measurement is why.

`scene::solveBodyCompensation` is a pure function from a list of `ReachDemand{root, target, reach}`
to one translation, applied by the stack before any limb runs. Authored as
`animation.bodyCompensation`. Measured on the real rig: a 10 cm step down clamps short with it off
and arrives with the body 0.0902 lower with it on.

### Corrections to this document, recorded rather than edited away

| §  | what it said | what the implementation found |
|---|---|---|
| §5.2 | hip adjustment is sixth in "what is missing" | it is second, behind the chain rule it depends on. 0.0098 m of slack is the reason |
| §12.2 | the fix is "one lambda" plus relaxing two checks | also the unconditional tip write, which nothing in the Phase 0 reading surfaced because it is invisible on a nested rig |
| §12.2 | the alien "needs a hand-authored map per rig pair" was the worst case | still open — the retarget work has not started — but the chain half of it is now data, not code |

### Steps 4 and 5 — contacts and phase · **DONE** (ADR-546, Accepted)

`src/scene/motion_analysis.{hpp,cpp}`: `detectContacts`, `extractPhase`, `analyseClip`. Offline
only. The textbook detector was measured wrong on this content and the finding is ADR-546's whole
context: **an in-place clip has no ground frame**, so "planted means stationary" finds the swing.

### Phase A task log

| task | state | note |
|---|---|---|
| STEP 1 arbitrary IK chains | **done** | ADR-543 Accepted; farm rigs unchanged, alien's detached chain solves |
| STEP 2 body compensation | **done** | ADR-544 Accepted; 0.0902 hip drop on the real rig |
| STEP 3 velocity vector | **done** | ADR-545 Accepted; measured once, not authored at 20+ sites |
| STEP 4 contact extraction | **done** | ADR-546 Accepted; the in-place finding |
| STEP 5 phase extraction | **done** | ADR-546; every clip in the pack yields a phase |
| STEP 6 phase-aware transitions | next | |
| STEP 7 inertialization | next | |
| STEP 8-9 decoupling, retarget profile | pending | the largest unit (§12.1) |
| STEP 10 100STYLE subset | pending | gated on 9 |
| STEP 11-13 MotionPack, offline tool, benchmark | pending | |
| STEP 15 visual validation | pending | the first non-numeric result |

### Still to come in Phase A

Steps 3-18 of the phase brief: velocity vector, contact extraction, phase extraction, phase-aware
transitions, inertialization, animation/asset decoupling, retarget profiles, the 100STYLE subset
experiment, the MotionPack, the offline tool, the M2 Max benchmark, and the visual lab scene.

---

## 21. References

### Papers and talks

| | |
|---|---|
| Büttner & Clavet, *Motion Matching — The Road to Next Gen Animation*, Nucl.ai 2015 | the origin, for *For Honor* |
| Clavet, *Motion Matching and The Road to Next-Gen Animation*, GDC 2016 | <https://gdcvault.com/play/1023280/Motion-Matching-and-The-Road> · transcript <https://archive.org/stream/GDC2016Clavet/GDC2016-Clavet_djvu.txt> |
| Bollo, *Inertialization: High-Performance Animation Transitions in "Gears of War"*, GDC 2018 | <https://media.gdcvault.com/gdc2018/presentations/bollo_david_inertialization_high_performance.pdf> |
| Holden, Kanoun, Perepichka, Popa, *Learned Motion Matching*, ACM TOG 39(4), SIGGRAPH 2020 | <https://doi.org/10.1145/3386569.3392440> · PDF <https://theorangeduck.com/media/uploads/other_stuff/Learned_Motion_Matching.pdf>. §3 is also the best written spec of **classical** MM in existence |
| Holden, Komura, Saito, *Phase-Functioned Neural Networks for Character Control*, TOG 36(4), 2017 | PFNN |
| Zhang\*, Starke\*, Komura, Saito, *Mode-Adaptive Neural Networks for Quadruped Motion Control*, TOG 37(4), 2018 | MANN |
| Starke\*, Zhang\*, Komura, Saito, *Neural State Machine for Character-Scene Interactions*, TOG 38(6), SIGGRAPH Asia 2019 | §4.3 |
| Starke, Zhao, Komura, Zaman, *Local Motion Phases for Learning Multi-Contact Character Movements*, TOG 39(4), 2020 | written because NSM's per-frame phase labelling was untenable |
| Starke, Mason, Komura, *DeepPhase: Periodic Autoencoders for Learning Motion Phase Manifolds*, TOG 41(4), 2022 | |
| Starke et al., *Categorical Codebook Matching for Embodied Character Controllers*, TOG 43(4), 2024 | |
| Hassan et al., *Stochastic Scene-Aware Motion Prediction*, ICCV 2021 | SAMP · <https://arxiv.org/abs/2108.08284> |
| Yuan et al., *PhysDiff: Physics-Guided Human Motion Diffusion Model*, ICCV 2023 | the penetration/float/skate metrics in §7 · <https://arxiv.org/abs/2212.02500> |
| Flash & Hogan, *The Coordination of Arm Movements*, J. Neuroscience 5(7), 1985 | the quintic in §2.8 |
| Shoemake, *Fiber Bundle Twist Reduction*, Graphics Gems IV, 1994 | quaternion inertialization |
| Jégou, Douze, Schmid, *Product Quantization for Nearest Neighbor Search*, 2011 | Büttner's 2019 quantised feature space |

### Code

| repo | licence | use |
|---|---|---|
| <https://github.com/orangeduck/Motion-Matching> | **MIT** | classical + learned MM, `nnet.h`, `spring.h`. **The only vendorable artifact in this study** |
| <https://github.com/orangeduck/Spring-It-On> | **MIT** | the damper/spring reference |
| <https://github.com/aaron1a12/wm-motion-matcher> | MIT | read the schema; expect it not to build |
| <https://github.com/E1P3/Learned_Motion_Matching_Training> | MIT | ONNX export bolted onto Holden's scripts |
| <https://github.com/SaxonRah/OpenMotion> | **none** | do not vendor |
| <https://github.com/pau1o-hs/Learned-Motion-Matching> | **none** | do not vendor |
| <https://github.com/E1P3/Learned_Motion_Matching_UE5> | **none** | do not vendor |
| <https://github.com/KamatMayur/UE5_MotionMatching> | MIT | contains no implementation |
| <https://github.com/sebastianstarke/AI4Animation> | **none; README says research/education only** | read the papers, not the repo |
| <https://github.com/facebookresearch/ai4animationpy> | **CC BY-NC 4.0 on the code** | read only |
| <https://github.com/mohamedhassanmus/SAMP> · `SAMP_Training` | **none, plus "may not redistribute"** | read only |

### Articles

Daniel Holden, <https://theorangeduck.com/>: *Code vs Data Driven Displacement* (the simulation
bone, adjustment, foot locking) · *Spring-It-On: The Game Developer's Spring-Roll-Call* ·
*Inertialization Transition Cost* · *Inverse Kinematics and Foot Locking* · *Propagating Velocities
through Animation Systems* · *Dead Blending*.

Epic, *Motion Matching in Unreal Engine*:
<https://dev.epicgames.com/documentation/en-us/unreal-engine/motion-matching-in-unreal-engine> ·
*Game Animation Sample*:
<https://dev.epicgames.com/documentation/en-us/unreal-engine/game-animation-sample-project-in-unreal-engine>

### Data

| | licence |
|---|---|
| **100STYLE** <https://zenodo.org/records/8127870> | **CC BY 4.0** |
| **ACCAD Open Motion Project** <https://accad.osu.edu/research/motion-lab/mocap-system-and-data> | **CC BY 3.0** (from OSU, *not* via AMASS) |
| **CMU Mocap** <http://mocap.cs.cmu.edu/> | free for all uses; no resale "even in converted form" |
| Mixamo <https://helpx.adobe.com/creative-cloud/faq/mixamo-faq.html> + Adobe General Terms §3.6, §17 | embed yes; redistribute no; train **no** |
| AMASS <https://amass.is.tue.mpg.de/license.html> | non-commercial; no distribution; **no commercial training** |
| LAFAN1 <https://github.com/ubisoft/ubisoft-laforge-animation-dataset> (`license.txt`, lowercase) | **CC BY-NC-ND 4.0** |
| SMPL-X <https://smpl-x.is.tue.mpg.de/modellicense.html> | non-commercial; patented (US10395411B2) |
| SAMP dataset <https://samp.is.tue.mpg.de/license.html> | non-commercial; viral onto trained weights |
