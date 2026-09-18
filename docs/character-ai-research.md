# Autonomous Character Intelligence: Phase 0 research

**Status:** Phase 0 complete. No production runtime code was written.
**Date:** 2026-09-17
**Branch:** `agent/character-ai-research`
**Deliverables:** this document, ADR-266..ADR-270, `src/entity/character_ai.hpp`,
`docs/character-ai-plan.md`, `tools/charai_probe.cpp`.

---

## 0. The finding that reorders everything else

The brief describes a twelve-layer stack to be built. Most of it is already here, under other
names, and the parts that are missing are not the parts the brief expects.

| the brief asks for | what exists today | where |
|---|---|---|
| character state | `entity::EntityState` + `Entity` | `entity/behavior.hpp:48`, `entity/entity.hpp:256` |
| navigation | `Navigator` + `NavGrid` A* + `ObstacleField` + `IPathProvider` | `entity/navigation.hpp`, `entity/nav_grid.hpp`, `spatial/obstacle_field.hpp` |
| steering / local avoidance | `Navigator::steer`, `crowdSeparation`, `resolvePenetration` | `entity/navigation.hpp:196`, `entity/entity.hpp:569` |
| behaviour state | `IBehavior` (11 kinds) | `entity/behavior.hpp:126`, `entity/behaviors.cpp` |
| action / intent | `ActionQueue`, 8 primitives, 3 authority tiers, resumption | `entity/action.hpp` |
| routines | `entity::Schedule`, pause-aware | `entity/action.hpp:393` |
| smart objects | `InteractionDesc`, authored **on the prop** | `entity/action.hpp:167` |
| blackboard | `PropertyDesc` -> `entity/<name>/state/<prop>` as real parameters | `entity/action.hpp:141` |
| directed performance | `stage::Staging`: 13 step kinds, queries, roles, beats, cycles | `stage/staging.hpp` |
| animation intent | `LocomotionState`, `IPoseSink` | `entity/locomotion.hpp:52` |
| gait / speed->clip | `Gait` with hysteresis, dwell, rate matching | `entity/gait.hpp` |
| AI level of detail | three bands per entity, **live** | `entity/entity.cpp:940-996` |
| goal taste | `InterestKind` + per-character affinities | `entity/entity.hpp:182`, `behaviors.cpp:1014-1018` |
| a lab to test it in | Engineering Lab Suite; `character` and `animation` labs registered | `src/labs/lab.cpp:30`, `docs/character-animation-lab.md` |

**Genuinely missing, in order of how much they cost:**

1. **Perception.** Nothing. There is no range, no facing, no occlusion, no awareness, no memory of
   having noticed anything. `EntityWorld::interestPoints()` is one omniscient list -- **505 entries
   on `glowmere-valley-2`** -- and every character scores all of it.
2. **A decider.** `ActionQueue` is a thing that can be *told* what to do. `stage::Staging` tells it,
   for authored scenarios. Nothing decides autonomously except `Explore`, which is 700 lines in one
   class with the goal model, the utility weights, the FSM, the replanner, the stuck detector, the
   jump arc and a novelty memory all fused together. This is why every autonomous character in
   Glowmere is an explorer: there is exactly one autonomous mind and it has one personality.
3. **Animation above one cross-fade.** No additive, no masks, no layer stack, no IK, no morph
   targets, no root motion extraction, and `ISkeletonQuery` has **zero implementations**.
4. **Social and group state.** `crowdSeparation` is the whole of it.

### Three claims in the assignment preamble that the code contradicts

These are corrections, offered because a correction is worth more than agreement.

- **"`world::TerrainQuery` has an `ObstacleField` seam that is not populated."** It is populated.
  `scene/composition.cpp:3650` fills `spatial::ObstacleField` from every scatter layer,
  `:3798` adds the heroes, `:3799` builds it and `:3804` installs the `world::ObstacleField` bridge.
  Measured on `glowmere-valley-2`: **1,238 solids, 1,238 of them blocking**. Both the navigator and
  `terrainQuery()` see them.
- **"Entity LOD is not live."** *Representation* LOD is not (`RepresentationSelector` has no call
  sites). *Behaviour* LOD is live, load-bearing, and is the single largest source of
  non-determinism in the entity layer -- see §E.4. Glowmere's walking characters carry
  `fullDetailDistance: 120`, `coarseInterval: 0.1`, `cullDistance: 340`.
- **"ADR-091 demands that a scrub equal a play."** ADR-091 demands that of the *baked* tier and
  explicitly concedes the opposite for the live tier: "Stateful, reset on seek, and **explicitly not
  frame-accurate under scrub**" (`ADR-091:64-66`). `EntityWorld::seek`'s own header promises only
  "the same seek time always produces the same state" (`entity/entity.hpp:617-619`). The good news
  is that the weaker promise undersells what the code actually achieves: §G shows play and seek
  already agree to 22 micrometres when the step sizes match.

---

## A. Navigation

### A.1 What this world is, which decides everything else

- **Terrain is an analytic height field.** `WorldMap::sample` returns height, normal, slope, the
  water surface and submersion from one set of noise evaluations, anywhere, exactly. It is
  regenerated from a seed and an editor moves it.
- **There is one walkable surface per XZ point.** `NavCell` carries a single `ground` float
  (`entity/nav_grid.hpp:60`). There are no overhangs, no bridges, no interiors, no second storey.
- **There is no triangle soup.** Obstacles are *cylinders*: `spatial::NavigationObstacle` is a
  centre, a radius, a base height and a height, classified by `ObstacleType`
  {Vegetation, Trunk, Rock, Structure, Creature, Custom} and by `Traversal`
  {Passable, StepOver, Jumpable, Blocking}. They come from the ecology scatter pass and from heroes.
- **Canopy is statistical.** `ClearanceField` answers "trees about nine metres tall grow around
  here", never "there is a trunk at this spot" (ADR-080). That distinction is load-bearing for §B's
  semantic model: a character cannot be told a fact the world does not hold.

### A.2 What already works, measured

`tools/charai_probe.cpp` on `examples/world/glowmere-valley-2.scene.json`, minima of 3 runs,
load average 3.75 (see §E.0 for what that load average does to these numbers):

```
navigation graph   154 x 154 cells at 4.00 m = 23,716 cells
                   walkable 19,584   water 1,200   blocked 2,479
                   regions 28, largest 9,938; build 243.9 ms
obstacle field     1,238 solids, 1,238 blocking

Navigator::requestPath (A* across the world)      24.969 us
Navigator::pathValid   (re-check a held route)    74.481 us
Navigator::steer       (fan of deviations)        59.135 us
Navigator::sample      (analytic world)           10.325 us
Navigator::groundHeight                            0.969 us
Navigator::clearanceAt (grid lookup)               0.024 us
Navigator::obstructed  (grid lookup)               0.011 us
```

Three things in that table are worth acting on.

**Re-validating a route costs three times what planning a new one costs.** `pathValid` samples the
analytic world along every remaining leg at 2.5 m spacing, each sample 10.3 us; `requestPath` is A*
over a grid that is already built. The replan-throttle in `Explore` (`repathSeconds` default 6.0)
is tuned as if the check were the cheap half. It is not.

**The navigation grid is the cheap spatial index nobody uses for anything but pathing.** A grid
lookup is 400x cheaper than an analytic world sample and ~150,000x cheaper than a sightline. A
perception layer that reads the grid is affordable; one that reads the world is not.

**Half the walkable ground is unreachable from the other half.** 28 regions, largest 9,938 of
19,584 walkable cells. That is an authoring fact nobody has been shown, and it is exactly what
`PathStatus::Unreachable` was built to report.

### A.3 Recast / Detour: the verdict is no

**Recommendation: do not adopt Recast, Detour, DetourCrowd or DetourTileCache.**

The decisive arguments are properties of this repository, not opinions about the library.

1. **Recast's input does not exist here.** Recast rasterises a triangle soup into a heightfield,
   filters walkable spans, builds regions and triangulates a navmesh. This engine has no authored
   collidable geometry to hand it. It would have to *generate* a triangle soup by tessellating the
   analytic terrain and extruding 1,238 cylinders into meshes -- fabricating an input in order to
   recover, approximately, the walkability the world already answers exactly.
2. **The one structural advantage of a navmesh is one this world cannot use.** A navmesh expresses
   several walkable surfaces over one XZ point. This world has exactly one, by construction, and
   `NavCell` says so with a single `ground` float. Every other advantage -- polygon-accurate
   boundaries, funnel-smoothed paths -- is a quality improvement over a 4 m grid whose local metre
   is already handled by `Navigator::steer`.
3. **A bake goes stale; this world moves.** The terrain is a function of a seed, an editor is being
   built that moves it, and the ecology pass re-scatters. `NavGrid` rebuilds in 243.9 ms and is
   correct by construction. A navmesh would be a build step somebody forgets to run, which is
   exactly the argument `entity/nav_grid.hpp:20-26` already makes and which the measurement supports.
4. **Determinism is a hard requirement here and not a documented Recast property.** ADR-091 needs
   the same request to give the same waypoints on any machine; `NavGrid`'s A* guarantees it by
   breaking ties on cell index. Detour makes no equivalent published guarantee, and its node pool
   and floating-point portal funnel would have to be audited before its output could be relied on
   under a scrub. **I did not test this** -- adopting the library to test it would be building the
   thing -- so it is recorded as an unretired risk rather than as a measured defect.
5. **Dependency analysis (§48), for completeness.** recastnavigation is zlib-licensed, CMake-built,
   roughly 30 kLOC across four modules, and runs on Apple Silicon in shipping titles. It is written
   in a C++98/03 idiom: it compiles under C++23 but will not pass this repository's warning
   settings without being wrapped as a system include, and `avgen_set_warnings` is applied per
   target so that is a real change to the build rather than a flag. It adds a build-time asset
   pipeline stage and a `.bin` navmesh artefact to a repository whose entire position is that
   world data is generated rather than stored. None of this is prohibitive on its own; it is simply
   cost paid for capability this world does not have.

**What would change the answer.** Authored interiors -- a building with two floors, a bridge over a
path, a cave -- where one walkable surface per XZ point is structurally wrong. If that is ever on
the roadmap, revisit this, and revisit it before the grid grows a second layer by accretion.

**The one Recast component worth wanting is `DetourCrowd`'s velocity-obstacle avoidance**, and it
is the one that cannot be taken alone: it operates on a `dtNavMesh`. The existing
`crowdSeparation` is a positional push, and ADR-240 §5 already found and fixed its worst defect --
a push that was allowed to be a whole walking step, measured at rook's worst backwards step
0.0933 m, exactly its authored walking step, reduced to 0.0317 m by making separation a *speed*.
If crowd avoidance needs to be better than that, the work is a velocity-space solver over the
existing `spatial::ObstacleField`, which is 300 lines, not a navmesh dependency.

### A.4 What navigation still owes

- **Dynamic obstacles.** `ObstacleField` is built once at load. A door that closes, a craft that
  lands, a fallen tree -- none can be added at runtime, and the grid would need a partial rebuild.
  This is the one Recast/TileCache feature with a real analogue here, and it is a rebuild-a-rect
  problem on a 23,716-cell grid, not a library problem.
- **Path smoothing.** Routes are cell centres. `Navigator::steer` hides this at walking speed; it
  will not hide it for anything that banks or turns on a radius.
- **Off-mesh links.** `NavSettings::jumpOver` and `NavCell::vault` (ADR-196) exist, are priced into
  A*'s cost term, and are **zero everywhere** -- no scene sets `jumpOver`, so the vault column of
  every cell is 0 and the cost term it feeds is exactly 0. Not to be confused with
  `explore/jumpRange` (ADR-194), which *is* authored -- `ember` carries 7.0 in the Glowmere scenes.
  The difference matters: a character can hop, and the *planner* still cannot route it across a gap.
- **Reachability reporting.** 28 regions and nothing says so. `NavGridStats::regions` is computed
  at build and read by no UI.

---

## B. Decision architecture

### B.1 What the references actually offer, and what this engine already has of it

**Unreal Behavior Trees** are a reactive tree over a blackboard: composites (selector, sequence,
simple parallel), decorators as conditional aborts, services on a cadence, tasks that return
succeeded/failed/in-progress. Their value is (a) authored control flow that a designer can read,
and (b) *conditional abort*, which is how a BT reacts without re-evaluating everything.

**Unreal StateTree** is the later answer to the BT's weakness: a hierarchical state machine whose
transitions are selectors, so the "which branch" decision is made once at a state boundary rather
than re-walked. Its value over a BT is that state is explicit and the evaluation is not a tree walk
per tick.

**Neither is adopted here, and the reason is that this repository already owns the expensive half
of both and none of the cheap half.**

`ActionQueue` (`entity/action.hpp:281`) already provides, today, in shipped and tested code:

- three authority tiers (`Routine`, `Action`, `Director`) with preemption,
- **resumption that keeps elapsed time** -- a routine paused at 10 s and resumed at 400 s fires its
  12 s entry two seconds later, not three hundred and ninety seconds late,
- eight primitives that are not verbs (verbs are `InteractionDesc` authored on props),
- start `Condition`s and `otherwise` branch labels,
- `ActionResult` {Completed, Skipped, Failed, Cancelled} with reasons,
- exclusive interaction claims,
- `ActionEvent`s on a timeline second, never a wall clock.

That is a BT's task-execution semantics, already written, already tested, already deterministic. A
behaviour tree on top would be a second interpreter with its own running/succeeded/failed enum
beside `ActionResult`, and the two would drift the first time somebody added a case to one.

And **authored control flow is already better served than a per-entity tree could serve it.**
`stage::Staging` expresses sequence (a cue's step list), parallel (a beat's cue list) and repeat (a
scenario's cycle) -- and its parallel is across *different entities*, which a per-entity BT
structurally cannot express (ADR-210 §3). The shipped UFO abduction is 24 parameters, 5 beats and
zero lines of C++ that know a saucer exists.

### B.2 What is actually missing: nobody decides

The gap is one layer, above the queue and below nothing:

> Given what this character can perceive and what it is, which of the things it could do does it do?

Today that question has exactly one answer in the engine, and it is hardcoded inside `Explore`:
five `InterestKind` affinities, a novelty radius, a stroll chance, a six-phase FSM, and a
`recent_` visit memory, all private to one 700-line class in `behaviors.cpp`. It works. It is also
why every autonomous character in Glowmere is an explorer with different weights, and why adding a
character that *guards* something, or *follows a routine and abandons it when startled*, means
writing a second 700-line class.

### B.3 Recommendation: a utility scorer over the existing queue

`IConsiderer` and `Option` in `src/entity/character_ai.hpp:§3`.

A considerer is handed a `DecisionContext` -- the character's state, its percepts, the navigator,
its seed -- and appends scored `Option`s. A selector picks the highest, subject to a dwell and a
margin, and pushes the winner's `ActionDesc` list onto `Authority::Routine`. That is the whole
mechanism. It is roughly 400 lines including the selector.

Why utility rather than a BT or a StateTree:

- **It composes.** Several considerers run in order and append to one list, so "what this species
  does", "what this scene asked for" and "what this shot needs" coexist without a merge rule.
  A tree composes by grafting, which requires a single owner of the tree.
- **It explains itself.** One axis means an overlay can print the losing scores beside the winner.
  "Why is it doing that" is the question this project has repeatedly been unable to answer about
  its own characters; `NavDebug` exists precisely because of that (ADR-197), and it was published
  and read by nobody for a while, which is the same as not existing.
- **It is already the house style.** `Explore`'s affinities are a utility model. The auto-director
  and the cinematic brief both choose with an order-free `(seed, index)` hash
  (`app/cinematic.cpp:1555-1587`), deliberately avoiding a PRNG stream so that adding one section
  does not re-cast the film. A scorer inherits that discipline directly; a tree with random
  selectors does not.
- **It is the smallest thing that removes the actual limitation.** `Explore` minus its hardcoded
  goal model *is* a considerer.

Three things that must be in the design and are easy to leave out:

- **Hysteresis is the selector's, not the considerer's.** A selector that took the top score every
  tick would thrash exactly as the gait machine thrashed before `GaitSettings::minDwell`. The rule
  is the one `Gait` already proved: a minimum dwell plus a margin to beat, not a tie.
- **A cooldown is a term in the score, not a flag.** The moment there is a second axis -- priority
  bands, interrupt flags -- the result stops being explicable and the overlay stops being useful.
- **Considerers hold no per-character state.** One instance per *kind* of character. That is what
  makes D4 free: there is nothing in a considerer to checkpoint.

### B.4 How many of the twelve layers are justified now

The owner's §55 preference is a small number of extremely solid capabilities. Applying it:

| layer the brief names | verdict |
|---|---|
| perception / sensing | **build** -- the only wholly missing foundation |
| memory | **build, minimally** -- bounded percept memory + the existing float properties. Not an episodic store |
| needs / drives | **defer** -- a need is a term in a utility score; it needs no layer of its own |
| emotion | **defer** -- it is a scalar on the score and a blend weight on the pose. When the pose layer can consume one, revisit |
| decision / planning | **build the scorer; reject the planner.** GOAP/HTN over eight primitives buys nothing a scored option list does not, and costs a search |
| navigation | **exists** |
| steering | **exists** |
| social / relationships | **defer** -- it is a per-pair weight in perception salience when it is anything |
| group / crowd | **exists** (`crowdSeparation`); improve to velocity space only if a shot needs it |
| animation selection | **exists** (`Gait` + `clips`) |
| animation layering | **build** -- it is the prerequisite for look-at, reactions and everything expressive |
| directed override | **exists** (`stage::Staging`, `Authority::Director`) |

**Four to build, six that exist, two to defer.** Not twelve.

---

## C. Animation architecture

### C.1 The inventory, verified

16 animated assets, 168 clips: `assets/imported/alien.gltf` (3), six `assets/aliens/*.glb` (26
each), nine `assets/farm/*.glb` (1 each, named `Walk`). The count is asserted only as `>= 10`
assets and `>= 100` clips (`tests/unit/test_character_lab_inventory.cpp:211-212`); the exact
figures live in a doc string and will drift silently.

The subsystem is five files, about 1,150 lines. What it has: per-joint TRS channels with glTF
Linear/Step/CubicSpline, slerp on the short arc, an `AnimationPlayer` with **two** clip slots and
**one** cross-fade, whole-pose lerp, a 256-joint palette, four-influence GPU skinning, and a
sample-time grid (`floor(now/step)*step`) so a 20 Hz rig gives identical matrices at 60 fps live
and 24 fps offline.

What it does not have, each verified by grep returning nothing in `src/scene/animation.*` and
`src/scene/skeleton.*`:

- **no additive animation, no masks, no layer stack** -- one cross-fade between two slots is the
  entire blending model, and three-way fades are explicitly refused (`animation.cpp:244-247`);
- **no IK of any kind** -- no two-bone, no foot, no aim, no look-at;
- **no morph targets** -- refused at both import paths (`assets/gltf_loader.cpp:404,548`);
- **no root motion extraction.** The engine's claim (ADR-161) that the content has none is
  measurably false for the current content: five alien clips travel, and `Landing`'s **-0.567 m**
  vertical drop is played in production and discarded;
- **`ISkeletonQuery` has zero implementations and `Entity::setSkeleton` has zero call sites.** Every
  `Entity::socketTransform` therefore returns the entity frame -- and returns `true` while doing
  it, so no caller can tell.

### C.2 `LocomotionState` is the interface, and it is already right

It carries activity, the action's activity name, playback rate, blend, the timeline second the
decision was made at, plus position, yaw, speed, turnRate, reaction and a look target. It is
published every frame into `IPoseSink`, and `AnimationSink::setLocomotion`
(`scene/composition.cpp:1892-1906`) reads exactly five of those fields. **`reaction`, `lookTarget`,
`hasLookTarget` and the kinematics are published and ignored.** The seam is wider than the
implementation on the far side of it.

So the animation-intent work is not interface design. It is, in dependency order:

- **A1. Implement `ISkeletonQuery` over `scene::SkinnedRig` and call `Entity::setSkeleton`.**
  The smallest change with the largest reach: it is the only thing between the engine and every
  socket, attachment, aim and carried prop.
- **A2. Make `socketTransform` report the fallback** instead of returning `true` on it.
- **A3. A two-slot layer stack with a joint mask**, so `reaction` and `lookTarget` can be consumed.
  This is the prerequisite for everything expressive, not an enhancement.
- **A4. Root motion extraction behind a per-clip opt-in**, because the content has it.

### C.3 Which position an animation layer may write

ADR-260's three positions is the rule that has cost this repository four separate defects, the
tractor beam being drawn 28.661 m from the body it was lifting being the most expensive.
`MotionAuthority` in `character_ai.hpp:§4` makes it an explicit, assertable value on any new layer:
`Simulation` writes `travel`, `VisualOffset` writes `MotionOffset`, `PoseOnly` writes neither.
Every clip is `PoseOnly` -- and root motion extraction (A4) is precisely the case where an
animation layer would have to ask for `Simulation`, which is why it needs an opt-in rather than a
switch.

---

## D. AV Gen integration map

### D.1 Where each stage runs, in the existing order

The behaviour update order, unchanged, with the new stages marked:

```
Composition::updateFields                      fields, before the routes (ADR-097)
Composition::updateBehaviour
  stage::Staging::update                       the authored director (ADR-210)
  observeCameraEvents                          (ADR-245)
  EntityWorld::update
    crowd rebuild                              a snapshot, before anything moves
 +  PERCEPTION                                 <- new. Cadenced, budgeted, before decisions
 +  DECISION                                   <- new. Scores options, pushes onto Authority::Routine
    LOD band                                   cull / coarse / full
    director (absolute)
    schedule
    actions                                    ActionQueue::update
    arc-entry
    behaviours   nav -> steer -> jump -> move -> separation -> penetration -> grounding
    director (additive)
    yaw wrap
    transform write                            applyOffsets -> node parameter finals
    arc-return
    gait                                       Gait::select, playbackRate
    locomotion publish                         -> IPoseSink
    attachments                                -> socketTransform
```

Both new stages sit **before** the LOD band deliberately, so that D3 can hold: LOD chooses whether
perception and decision run this step, and never how large a step the integrator takes.

### D.2 Where the definitions live: scene, project, or a third place

ADR-264 is the constraint: a project's `parameters` block is applied **over** the scene's, the
multicam project carries 5,489 of them, and every test in this repository loads a scene while the
owner opens a project.

**Recommendation: character AI definitions live in the scene, and nothing about them lives in a
project's `parameters` block.**

- A `PerceptionSettings` and a considerer list are *structure* -- what a character is -- exactly as
  `behaviors`, `clips`, `gait` and `schedule` already are, and those are all scene keys today.
  Adding a sixteenth scene key next to fifteen is a smaller change than inventing a third file.
- The **numbers** inside them should be `params::Parameter`s registered under
  `entity/<name>/perception/<knob>`, following ADR-011 and the existing `entity/<name>/<behaviour>/`
  convention. That makes them keyframeable, presettable and modulatable for free -- and it means a
  project *can* override them through the mechanism that already exists, deliberately, rather than
  through a new one.
- A third file is refused for the reason `profileFromJson` already exists: the reusable half of a
  character is a **profile**, and a profile library is already a supported file. A perception
  profile is a profile.

The one thing that must not happen: no test may be written that loads only a scene and concludes a
character behaves a certain way. Every Character Intelligence Lab case must be able to name a
project, the way the beam probe's `AVGEN_BEAM_LAB_PROJECT` now can.

### D.3 The Character Intelligence Lab

**Do not build a new testing application.** Register a lab, per ADR-261, which is four things and
zero engine code:

1. an enumerator in `labs::LabId` (`src/labs/lab.hpp:32`),
2. a descriptor in the `constexpr std::array` at `src/labs/lab.cpp:30`, in enum order, carrying
   `owns`, `doesNotOwn`, a `decides` `path:symbol` that the registry test greps for, a `doc`, a
   `fixture` and a `cases` path,
3. a `case` in the exhaustive switch in `src/labs/overlays.cpp`,
4. `examples/labs/character-ai/cases.json` plus tests under `tests/unit/`.

Plus one line in `tests/unit/test_lab_registry.cpp:149` to bump the count. **No CMake change** --
`src/labs/*.cpp` is globbed.

Two things the registry already gets wrong that this work should fix while it is in there: the
`animation` and `character` labs' `doc` fields point at `docs/engineering-labs.md` rather than at
`docs/character-animation-lab.md`, and their `fixture` fields name `examples/characters/alien.json`
rather than the lab's actual `examples/lab/character-animation-lab.json`. The registry test cannot
catch it because both targets exist.

### D.4 The AI control plane has no staging or character tools

`src/ai/` is an LLM control plane over 58 schema'd engine tools. `grep -rn "staging\|director"` over
it returns only the word "directory". It can reach parameters, scene nodes, camera, sequencer,
modulation, fields and world recipes; it cannot start a scenario, author a beat, read a role
binding, or ask what a character is doing. A scenario's *numbers* are reachable today because
`staging/<scenario>/<name>` are ordinary parameters. Its *structure and control* are not. That is a
clean seam for a later phase and is not on the critical path.

---

## E. Performance model

### E.0 How these numbers were taken, and what is wrong with them

`tools/charai_probe.cpp`, CPU only, no GPU, no window. Minima over repeats, never means (ADR-170).
`examples/world/glowmere-valley-2.scene.json` with the real navigator, the real 23,716-cell grid
and the real 1,238-solid obstacle field.

**Load average during the scaling run was 3.75-4.85 on an otherwise idle machine that was also
running four agents.** Every number below is therefore an *upper* bound: a quiet machine will be
faster, and the ratios between arms are more trustworthy than the absolute milliseconds. Where a
number is used to make a decision below, the decision survives a 2x error in it.

Every arm has a control. The scaling arms report the furthest distance any body actually walked:
`nil` must read 0 m and the others must not. Two arms could not fail when first written and both
are recorded in the commit message, because a probe that cannot fail proves nothing (ADR-182).

### E.1 Measured: cost per behaviour update, 60 Hz, `distanceDetail` off

Milliseconds for the whole population, one 1/60 step:

| profile | 10 | 25 | 50 | 100 | 250 | 500 | per character |
|---|---|---|---|---|---|---|---|
| `nil` (no behaviours) | 0.0003 | 0.0007 | 0.0014 | 0.0033 | 0.0082 | 0.0163 | **0.03 us** |
| `wander` + `ground` + `liveliness` | 0.1403 | 0.3616 | 1.0693 | 2.5877 | 4.0503 | 9.3657 | **~19 us** |
| `explore` + `liveliness` + `lookAt` | 0.3656 | 1.9611 | 2.4175 | 7.8796 | 23.5162 | 44.3583 | **~89 us** |

Control: max travel at N=50 was 0.00 m for `nil`, 1.03 m for `wander`, 3.08 m for `explore`.

The per-character figures are the slope of the upper half, not the ratio at one point; the arm is
noisy at small N because a single route plan (25 us) dominates a 10-body frame.

**Read against a 16.7 ms frame:** the harness floor is free. A `wander` population costs about
1% of a frame at 100 and 56% at 500. An **`explore` population costs 47% of a frame at 100 and
exceeds the whole frame somewhere around 180**. That is the honest ceiling on the current
architecture, and it is a *behaviour* cost, before any perception or decision layer exists.

### E.2 Extrapolated: what perception and decision would add

Labelled as extrapolation, with the basis stated.

- **Perception, grid-backed, 4 Hz, 8 percepts, no occlusion.** Basis: a candidate scan is a radius
  query over the existing `spatial::PointGrid` plus one `clearanceAt` (0.024 us) and one distance
  test per candidate. At 505 interest points, a 60 m radius on a 600 m world selects order 1% of
  them, call it 8-20 candidates. Estimate **2-4 us per character per sense tick**, which at 4 Hz is
  **0.13-0.27 us per character per frame** -- amortised, under 1% of the `explore` cost. Perception
  is affordable. This estimate is dominated by the radius query, which I did not isolate; it is the
  one number here I would want measured before committing to a 60 Hz variant.
- **Perception with occlusion, measured directly.** `world::heroSightline` is 1720.779 us at 20 m,
  5391.917 us at 60 m, 16907.755 us at 200 m -- it marches at a fixed step in metres, so cost is
  linear in range. **One test per character per frame at 100 characters is 172 ms a frame.** This
  is not a tuning problem; it is a structural one. Occlusion testing must be a
  round-robin with an explicit per-world budget, and `Percept::tested` must say when it did not
  happen rather than reporting `visibility = 1` as if it had.
- **Decision, a scorer over 8 percepts x 6 considerers at 2 Hz.** Basis: scoring is arithmetic over
  a cached percept list with no world queries by contract. Estimate **1-3 us per decision tick**,
  i.e. **0.03-0.1 us per character per frame**. Negligible. The cost of a decision is not the
  scoring, it is what the winner then asks navigation for -- and `requestPath` is 24.969 us. A
  world where 100 characters each replan once a second costs **2.5 ms/s**, or 0.15% of a frame.

**Consolidated model, per character per frame, at 60 Hz:**

| stage | cost | basis |
|---|---|---|
| entity bookkeeping | 0.03 us | measured |
| `wander`-class behaviour | 19 us | measured |
| `explore`-class behaviour | 89 us | measured |
| perception, 4 Hz, no occlusion | 0.13-0.27 us | extrapolated from grid query costs |
| perception with occlusion, 1 test/char/frame @ 20 m | 1720 us | measured; **rejected as unaffordable** |
| decision, 2 Hz | 0.03-0.1 us | extrapolated |
| replanning, 1 Hz | 0.42 us | measured `requestPath` / 60 |

At 10/25/50/100/250/500 characters, `explore`-class, with perception and decision on and occlusion
off: **0.90 / 2.2 / 4.5 / 9.0 / 22.4 / 44.8 ms per frame** -- which is the measured behaviour cost
plus under 1%. **The new layers are not the cost. The existing behaviour is.**

### E.3 Where the 89 us actually goes, and what to do about it

`explore` per step does: `steer` (59.135 us) most frames, `sample` (10.325 us) several times, a
crowd query, a grounding snap, and `pathValid`/`requestPath` on a throttle. The two that dominate
are `steer` and `sample`, and both read the **analytic world** when the **grid** holds the same
answer 400x cheaper for the questions being asked. The optimisation with the most headroom in this
whole document is: *steer and validate against `NavGrid`, and reserve the analytic world for the
final ground snap.* That is not part of the character AI work, but it is what decides whether the
cast can be 100 or 500, so the plan sequences it explicitly (Phase 6).

### E.4 The scrub budget

`Engine::seekSeconds` re-simulates the entity world forward at a fixed 1/60 from up to **90 s**
back, synchronously, on the main thread (`app/engine.cpp:2298-2300`). At 23 entities that is 124,200
behaviour updates per click and `docs/investigations/ui-responsiveness.md` measured 165 ms to 5,003
ms per timeline click, with the re-simulation being **99.999%** of the seek.

I extrapolated this from §E.1's per-character figures first, then measured it, and the extrapolation
was wrong by a factor of two to five. Both are shown, because the gap is the finding.

One `EntityWorld::seek(90 s)` -- 5,400 fixed steps -- minima of 2 runs, load average ~4:

| cast | `wander` extrapolated | `wander` **measured** | `explore` extrapolated | `explore` **measured** |
|---|---|---|---|---|
| 10 | 1.0 s | **1.45 s** | 4.8 s | **9.10 s** |
| 50 | 5.1 s | **11.08 s** | 24 s | **64.63 s** |
| 100 | 10 s | **19.38 s** | 48 s | **161.51 s** |
| 250 | 26 s | **60.79 s** | 120 s | **594.66 s** |

**Why the extrapolation was low.** §E.1 measures a *warm steady state*: one untimed step is taken
first, so every character already holds a route, and the 60 measured frames are mostly steering
along it. A seek starts from `reset()` and covers 5,400 steps, during which every character plans
from scratch, arrives, re-selects a goal and replans many times over -- each cycle costing a
`requestPath` at 24.969 us and repeated `pathValid` at 74.481 us. Per-frame steady-state cost is
the wrong unit for a cold 90-second replay, and using it understates by 2-5x.

**Two hundred and fifty explorers is a ten-minute stall for one timeline click.** One hundred is
two minutes and forty seconds.

**This, not the frame cost, is what caps the cast size in an editor.** At 100 `explore`
characters the frame cost is 9 ms and the scrub cost is 161 seconds: a ratio of about eighteen
thousand. Three mitigations, in order of value:

1. **Cap the re-simulation by work, not by seconds.** `maxSeconds = 90.0` is a literal, and it is
   the wrong unit: it should be a budget in entity-steps, so a 23-body scene keeps its 90 s of
   history and a 250-body scene keeps what it can afford.
2. **Move the re-simulation off the main thread** and show the last good frame while it runs. The
   existing `JobSystem` is the vehicle; the entity world is not currently safe to simulate off-thread
   and making it so is a real piece of work.
3. **Checkpointing** -- and note that this is an *optimisation*, not a correctness mechanism.
   §G shows the replay is already exact.

---

## F. Recommendation, and what is rejected

### F.1 The recommendation

**Four layers, not twelve. Two new, one extended, one left alone.**

- **Sense** (new, `IPerception`): budgeted, cadenced, grid-backed, occlusion off by default and
  honest about it.
- **Decide** (new, `IConsiderer`): a scored option list composed from several considerers, a
  selector with dwell and margin, output is `ActionDesc`s onto `Authority::Routine`.
- **Act** (unchanged): `ActionQueue`, the eleven behaviours, `Navigator`, `stage::Staging`.
- **Express** (extended): `ISkeletonQuery` implemented, sockets made real, a two-slot masked layer
  stack so `reaction` and `lookTarget` stop being published into nothing, root motion behind an
  opt-in.

Plus the determinism work in §G, which is not a layer but is the precondition for all of it.

### F.2 Rejected, with reasons

| rejected | why |
|---|---|
| **Recast / Detour / DetourCrowd** | §A.3. No triangle soup to build from; one walkable surface per XZ by construction; a bake in a world regenerated from a seed; determinism not a documented property; the existing grid is measured working at 24.969 us a query |
| **A behaviour tree** | This repository owns BT task-execution semantics already (`ActionQueue`: tiers, preemption, resumption with elapsed time, conditions, branch labels, results with reasons). A tree would be a second interpreter with a second result enum |
| **StateTree / a hierarchical FSM as the top layer** | Same objection, plus: `Explore` already *is* a hierarchical FSM and its problem is not its shape, it is that its goal model is fused into it |
| **GOAP / HTN planning** | A search over eight primitives whose preconditions are float comparisons. A scored option list gets the same answers without a search, and a planner's output would still have to be an `ActionDesc` list |
| **A new entity system** | §4 of the brief is emphatic and correct. `EntityWorld` is 2,205 lines of tested, ordered, LOD-banded, parameter-bound simulation |
| **A new testing application** | ADR-261. Register a lab |
| **A separate character-AI file format** | ADR-264. A profile is already a file; a scene key is already the convention; a project's `parameters` block already overrides |
| **Per-character per-frame line of sight** | Measured at 1720.779 us per test. 172 ms a frame at 100 characters |
| **Making the AI a pure function of (seed, time)** | §G. It cannot be -- a route round an obstacle has no closed form -- and it does not need to be |
| **Episodic memory / needs / emotion as layers** | §B.4. Each is a term in a score or a blend weight, not a subsystem. Revisit when the pose layer can consume one |
| **Animation blend spaces / 2D directional blending** | The engine has one cross-fade between two slots. A blend space is three steps past the layer stack that does not exist yet |

---

## G. Determinism vs. autonomy: the central question, answered

### G.1 The measurement

`tools/charai_probe.cpp`, 8 `explore` characters on the real world, worst position difference at
t = 30 s, load average 3.75:

| comparison | worst difference | |
|---|---|---|
| play(60 Hz) vs play(60 Hz) again | **0.000000 m** | control: must be 0 |
| play(60 Hz) vs play(30 Hz) | **0.955805 m** | control: must NOT be 0 |
| seek(60 Hz step) vs seek(60 Hz step) again | **0.000000 m** | |
| **play(60 Hz) vs seek(60 Hz step)** | **0.000022 m** | the claim under test |
| play(jittered 45-90 Hz) vs seek(60 Hz step) | **0.094877 m** | what a real session plays |
| seek(60 Hz step) vs seek(30 Hz step) | 0.955803 m | |
| play(LOD on, camera at origin) vs (camera at 200 m) | **50.263096 m** | |
| play(LOD on, near) vs play(LOD on, past the cull band) | 110.622116 m | a culled body never moves |

### G.2 The verdict

**Scrub-exact autonomous characters are achievable, the architecture already nearly achieves it,
and the cost is three specific changes -- none of which is about autonomy.**

The simulation is already a deterministic replay: same seed, same ordered steps, same answer, to
22 micrometres over half a minute. What breaks it is not memory, not accumulated state, not
planning. It is:

1. **The frame rate.** Play integrates the real frame delta (`scene/composition.cpp:2049`); seek
   forces 1/60 (`entity/entity.cpp:721`). A jittered session diverges by 0.095 m over 30 s.
   **Fix: a fixed simulation step with an accumulator.** The entity world advances in whole 1/60
   steps and the frame rate decides how many, not how big. This is the single most important change
   in the whole plan and it is perhaps 60 lines.
2. **Behaviour LOD.** 50.263 m. The coarse band integrates one 0.1 s step where full detail
   integrates six of 1/60, through a steering function that is not linear in dt -- and the band is
   chosen from the *camera position*, so the simulation depends on where somebody was looking.
   `src/entity/action.hpp`'s claim that "behaviour LOD does not change the answer" is measurably
   false for behaviours. **Fix: LOD selects which stages run -- perception, decision, replanning --
   never the integration step.** A far body integrates the same 1/60 steps with a cheaper mind.
3. **Four smaller seek defects**, each real, each cheap:
   - `BehaviorContext::self` is left at its default 0 during a seek (`entity.cpp:747-753` omits the
     assignment `entity.cpp:1049` makes), so every body excludes entity 0 from crowd separation
     instead of itself;
   - the crowd field is never rebuilt during a seek, so a seek separates against a snapshot from
     whatever frame last played;
   - the action queue and schedule are not re-simulated at all -- the `Cinematic Action` tier is
     reset to the authored list and integrated by nothing;
   - seek reads `viewPosition` for its cull test, so it is a function of (time, camera).

### G.3 Therefore

**The AI must be a pure function of (seed, the ordered sequence of fixed simulation steps).** Not of
(seed, time) -- a route round an obstacle has no closed form and pretending otherwise means throwing
away the obstacle avoidance that makes it worth watching, which `entity/entity.hpp:601-606` already
says. The sequencer's camera bake is not a model to copy for characters; it works because a
`seq::Actor`'s position is `positionAt(t)`, and `sequence.hpp:145-150` already states that a node
moved by an entity behaviour is explicitly not that.

**Checkpointing is not required for correctness.** The replay is exact. Checkpointing is an
*optimisation for scrub latency*, and §E.4 says what it would buy: at 100 `explore` characters, a
48-second stall per timeline click. Recommend it as Phase 8, priced against moving the
re-simulation off the main thread, which may be the better spend.

**The four obligations on every new type** are written into `src/entity/character_ai.hpp:§1` as
D1-D4: no wall clock; draw from (seed, index) and never from a stream for anything a decision
depends on; LOD changes which stages run and never the step; memory is bounded and reconstructed by
the replay rather than persisted.

---

## H. Unsupported: what the brief asks for that this engine cannot do

Stated plainly, with what each would take.

| asked for | why it cannot be done now | what it would take |
|---|---|---|
| A character looking at something | `lookTarget` is published and read by nothing. There is no additive layer and no joint mask, so there is nowhere to put a head turn | A two-slot layer stack with a mask (A3). ~400 lines in `scene/animation.*` |
| A character holding, carrying or wearing anything correctly | `ISkeletonQuery` has no implementation; every socket returns the entity frame and says `true` | A1 + A2. ~150 lines |
| Animation that moves the body | Root motion is never extracted. `Landing`'s -0.567 m is discarded every time it plays | A4, behind a per-clip opt-in, writing `MotionAuthority::Simulation` |
| Facial expression, emotion on the face | Morph targets are refused at import on both paths | An import path, a `weights` channel on `Pose`, a GPU path. Large; not recommended |
| Occlusion-aware perception for a crowd | 1720.779 us per test at 20 m | Round-robin with an explicit per-world budget, and `tested` reported honestly. Never per-character per-frame |
| A character noticing a specific tree | The canopy is statistical (ADR-080): the world can say "nine-metre trees grow around here", never "there is a trunk at this spot". Per-instance solids exist in `ObstacleField` but carry no identity beyond `ObstacleType` | Give `NavigationObstacle` a stable id and an index. Cheap, but it makes the obstacle field a scene object rather than a derived one |
| A door that closes, a craft that lands, blocking a route | `ObstacleField` is built once at load and the grid is baked from it | A partial rebuild over an XZ rect. Bounded work on a 23,716-cell grid |
| Two-storey interiors, bridges, caves | `NavCell` has one `ground` per XZ | This is the one case that genuinely wants a navmesh. Revisit §A.3 |
| A scrub that matches a played timeline with audio | `EntityWorld::seek` passes a null bus deliberately: applying one instant of the music uniformly across 90 s is not a replay | Replaying the analysis, which means the offline analyser running on the seek path |
| 500 autonomous `explore` characters at 60 Hz | Measured 44.36 ms per frame for behaviours alone | §E.3: steer and validate against the grid rather than the analytic world |

---

## I. What I could not answer

- **Whether Detour's pathfinding is bit-reproducible across builds.** Adopting it to find out is
  building the thing. Recorded as an unretired risk in §A.3; the recommendation does not depend on
  it, because four other arguments are decisive on their own.
- **The cost of the radius query in a perception scan.** §E.2's 2-4 us estimate is dominated by a
  `spatial::PointGrid` query I did not isolate. It is the one extrapolated number I would measure
  before Phase 2 starts, and it is one probe arm.
- **What the numbers are on a quiet machine.** Every timing here was taken at a load average
  between 3.75 and 4.85. They are upper bounds; the ratios are sound and no decision in this
  document turns on a factor smaller than two.
- **Nothing here about the scrub cost is still extrapolated** -- it was, and the measurement
  corrected it by a factor of two to five (§E.4). Treat §E.2's perception and decision estimates
  with the same suspicion until somebody measures them; they are steady-state estimates of the same
  kind that was wrong once already, although they are estimates of arithmetic rather than of
  planning, which is the part that turned out to be underweighted.
- **Whether the 0.000022 m residual between play and seek is float ordering or a real divergence.**
  It is below the scale of anything visible and is not worth a phase, but it is not explained.
