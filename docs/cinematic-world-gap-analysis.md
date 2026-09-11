# Cinematic world engine: audit and gap analysis

Phases 1 and 2 of the *All You Got* architecture expansion brief, plus its Night Shift addendum.
This document is the written matrix the brief asks for before any code is written.

Everything in the "Today" column was verified by reading the source on `main` at the commit that
precedes this document, not recalled. Where something was checked and found absent, it says so.

## The headline

**The brief asks for roughly forty systems. About half of them already exist**, several under names
the brief does not use, and two of the ones it treats as the hardest new work — the cinematic shot
model (§15, §16) and reusable audio-reactive entities (§20, §39–§43) — are largely built and
shipping in examples today. The genuinely missing systems cluster in three places: **pathfinding and
collision**, **the city as a graph** (roads, vehicles, interiors), and **the action/schedule layer**
that turns a character into an actor with intentions.

There is also one architectural question that the brief does not name and that everything else
depends on. It is in §3 below, and it should be settled before Phase 3 starts.

---

## 1. What already exists

### The sequencer (ADR-089) — `src/seq/`

`seq::Sequence` holds `SceneSlot`, `Shot`, `Actor`, `Transition`, `Marker` and `OverlayCue`, and
**bakes** them into ordinary `params::Track`s. This covers the brief's §15 (Shot), §16 (hierarchy)
and most of §44 (director API) as data rather than as code.

The bake is the single most important existing decision, because it is what already answers §26
(clock separation), §27 (determinism), §22/§55 (scrubbing and offline) *by construction*: after a
bake there is no accumulated state, so frame 1000 is the same frame whether the playhead walked
there or jumped. The brief's testing section asks for scrub and replay guarantees that the
sequencer already provides — for everything that goes through it.

A `Shot` carries name, start, duration, scene slot, camera, in/out transitions and its own
parameter tracks. An `Actor` carries position keys, an arc-length-parameterised `ActorPath` spline,
and `ClipCue`s naming animation states. `ShotCamera` can inherit, take hand-authored keys, or take
an `app::Shot` move — and `lookAtActor` already tracks a *moving* subject, which is §14's tracking.

### The cinematic camera vocabulary (ADR-062/071) — `src/app/cinematic.hpp`

Fourteen `ShotKind`s (Establish, Approach, Reveal, **Entry** — "travel into the interior of
something" — Passage, Descent, Ascent, Orbit, Track, Discovery, HeroReveal, Flyby, Drift,
Transition), five `LookMode`s, four `MovementCurve`s, easing at both ends, and `FocalTarget` with a
radius so a shot is reusable against a subject of any size.

The storyboard's hardest camera move — rise through the city, approach the window, enter the
building — is `Ascent` then `Approach` then `Entry`, which is vocabulary that exists.

### Camera optics — `src/scene/post_settings.hpp`

Physical depth of field (`dofPhysical`, `focusDistance`, `dofMaxRadius`), **tilt-shift** (ADR-079,
the miniature look the *All You Got* concept is built on), motion blur, halation, anamorphic
streaks, and a full grade. §14's list is satisfied except camera shake, which does not exist
anywhere in the codebase (`cinematic.hpp` says so deliberately: "there is no shake in this file").

Rack focus is a keyframe on `post/dof/focusDistance`, which is already a parameter.

### The entity layer (ADR-088) — `src/entity/`

Much closer to the brief than the brief assumes. `EntityDesc` already carries:

- `behaviors` — `hover`, `drift`, `bank`, `spin`, `wander`, `interest`, `orbit`
- `reactions` — `ReactionDesc{signal, target, depth, op, polarity, ProcessorChain}`; **this is
  §20's reaction profile**, and it compiles down to an ordinary `ModRoute` (`fromEntity`)
- `sockets` / `attachments` — **§37 and §38 exist**: a named socket on a joint, and a composition
  node carried by it
- `clips` — activity name → clip name, with an explicit comment that a behaviour must never name a
  clip because "the same `wander` has to drive an alien, a deer and a robot". **This is §36's
  animation mapping**, already argued for on the brief's own grounds.
- `profile` — a named bundle of behaviours/reactions/clips/sockets an entity is built from, with
  round-trip bookkeeping so saving does not inline what the author shared. **This is §20's
  "reusable reaction profile" as a first-class concept.**
- `fullDetailDistance`, `coarseInterval`, `cullDistance` — **§29 simulation LOD already exists** for
  behaviour: beyond a distance an entity updates every N seconds with accumulated dt, past another
  it does not update at all. `EntityUpdate::viewPosition` feeds it.

`LocomotionState` carries `Activity{Idle, Walk, Run, Turn, Observe, React}`, position, yaw, speed,
turn rate, a reaction level and a look target — **§7's locomotion state machine, with intent
(speed) selecting the gait**, which is exactly the shape §7 asks for.

`IBehavior::reset()` exists and is documented as being called on a timeline seek.

### Navigation, partially — `src/entity/navigation.hpp`

`Navigator` samples walkability (`maxSlope`, `waterMargin`, `headroom`, `stepHeight`,
`walkableVegetation`, hero and boundary margins) and returns a typed rejection reason. It can pick a
random valid destination, test whether a straight line is clear, and `steer()` locally.

**There is no graph and no search.** See §2.

### World queries (ADR-080/090) — `src/world/`

`WorldMap::sample()`, `ClearanceField` (statistical canopy), and as of this week `TerrainQuery` —
the single shared spatial-query surface — plus `ObstacleField` as the seam for per-instance
occupancy and `WaterCourse` for rivers. §5's "terrain adherence" and §11's terrain half are covered.

### Signals and modulation

`SignalBus::declare()` takes a name at runtime, so **new signals can be published without touching
the bus** — which is how a music field can expose itself without a second analysis path (§10/§23 of
the addendum, §20 of the brief). `ModRoute` + `ProcessorChain` (gain, curve, clamp, threshold,
asymmetric attack/decay, envelope, remap, depth) is the canonical reactivity path, and musical
events (ADR-073) already publish beat/bar/section as signals.

### Other

Particles (§42's substrate), `spatial::Spline` (paths), the job system, async world building,
offline rendering with a deterministic `FixedStepClock`, and `comp::LayerStack` for 2D overlays and
the fourth-wall UI of §3.

---

## 2. The matrix

Complexity is S (days), M (a week-ish), L (multi-week). "Perf" is the risk the system introduces if
built naively.

| # | Requirement | Today | Gap | Proposed extension | Cx | Perf | Depends on |
|---|---|---|---|---|---|---|---|
| §5 | Navmesh / walkable surfaces | `Navigator` samples walkability per point | No connectivity: nothing knows two walkable points are reachable | Walkability **grid** over `TerrainQuery`, flood-filled into connected regions; islands become explicit | M | Build cost once per world; cache | TerrainQuery |
| §6 | Pathfinding | `pathClear` (straight line), `steer` (local) | **No search, no waypoints, no replanning, no stuck detection** | A* over the §5 grid, funnel-smoothed to a waypoint list; replan on `ObstacleField` change; give-up with a reason | M | Must be off the render thread or budgeted per frame | §5 |
| §7 | Locomotion state machine | `LocomotionState` + `Activity` + clip mapping | Blending between gaits is thin; no accel/decel model | Gait selection by speed with hysteresis; `AnimationPlayer` cross-fade already exists | S | Negligible | — |
| §4 | Action system | — | **Nothing.** No action, target, completion, interruption or queue | `entity::Action` queue: `{kind, target, clip, duration, onComplete}`, ticked by the entity, drained in order | M | Negligible | §7 |
| §9 | Schedules / routines | — | **Nothing** | A schedule is a time-ordered list of actions; feed the same queue | S | Negligible | §4 |
| §10 | Interaction system | Sockets/attachments exist | No interaction verbs, no target sockets on props, no conditions | `InteractionDesc` on a prop node: verb, socket, clip, duration; an action targets it | M | Negligible | §4, §37 |
| §11 | Character collision | Terrain adherence; `ObstacleField` seam defined | No capsule, no agent-agent avoidance | Capsule vs `ObstacleField` + local separation between agents via a uniform grid | M | Grid, not N² | §5 |
| §12 | Vehicles | — | **Nothing** | Lane-follower on the §13 graph: spline position, speed, a car in front, a light ahead | M | Cheap if kinematic | §13 |
| §13 | Road / city graph | — | **Nothing** | `city::Graph` of nodes/edges with lane, sidewalk, crossing, entrance, stop kinds; both characters and vehicles path on it | L | Build once | — |
| §14 | Cinematic camera | Nearly complete (see §1) | **Camera shake only** | A shake as a camera-space offset behaviour, amplitude as a parameter | S | Negligible | — |
| §15/§16 | Shot / sequence hierarchy | **Exists** (`seq::Sequence`) | Terminology only: brief's "Scene" = `SceneSlot` | Document the mapping; do not rename | — | — | — |
| §17 | Cinematic events | Markers are labels; tracks fire values | No event → action dispatch; no `AnimationComplete`, no volume events | `seq::EventTrack`: `{when, what}` where *when* is time/beat/bar/section/shot edge/volume/completion and *what* is an action, a parameter set, a cue | M | Event-driven, not polled | §4, §18 |
| §18 | Spatial triggers | — | **Nothing** | `TriggerVolume{sphere\|box\|capsule, filter, falloff}` evaluated against entity positions in a uniform grid; enter/exit edges | M | Grid; O(entities) | — |
| §19 | **Music influence field** | — | **Nothing** — but see §3 below | A field is a position, a radius, a falloff curve and a strength; entities read their own influence and it **scales the depth of their existing reactions** | M | One grid query per entity per frame, LOD'd | §18 |
| §20 | Reaction profiles | **Exists** (`EntityDesc::profile` + `reactions`) | Profiles are per entity; no shared library file | A profile library the scene references by name | S | — | — |
| §21 | NPC music reaction | Behaviours + reactions exist | No "enter reaction, hold, return to previous state" arc; no per-entity variation | An `Action` of kind `react` pushed by a field crossing, with per-entity seeded delay/duration/intensity | M | — | §4, §19 |
| §22 | Music propagation | — | Secondary emitters | Architect only: a field's source is an entity property, so an influenced entity can own a field | S | — | §19 |
| §23 | Audio signal access | **Exists** | None | — | — | — | — |
| §24 | Musical sections | Markers exist; `music.*` signals exist | Sections do not *drive* anything | Section becomes an event source in §17 | S | — | §17 |
| §25 | Multi-audio | Exists | Verify field/event timing uses timeline time, not source time | Audit | S | — | — |
| §26 | Clock separation | `renderTime`, `FixedStepClock`, seek | Simulation time is not separable from render time | See §3 | M | — | — |
| §27 | Deterministic simulation | Entity seeds derive from scene seed + name | Behaviours accumulate; seek resets them | See §3 | M | — | — |
| §28 | Population management | — | **Nothing** | Spawn regions + an active set chosen by camera distance, with hysteresis bands | M | This is the one that protects frame time | §29 |
| §29 | Simulation LOD | **Exists for behaviour** (distance → coarse interval → cull) | Not applied to animation or navigation; no LOD for vehicles | Extend the same three-band model to rig updates and path ticks | S | Saves cost | — |
| §30/§31 | Interiors | Scene slots switch by visibility | No building metadata, no entrance/room concept | `BuildingDesc` on a node: entrances, interior slot, accessibility tier | M | Visibility only | §16 |
| §32 | Shot-aware streaming | `SceneSlot::file` recorded "so a future preloader knows what a slot is" | The preloader | Use the recorded hook; keep everything resident for v1 as the sequencer already decided | M | — | — |
| §33 | Culling | Frustum, distance, screen-size, LOD hysteresis (ADR-082) | Occlusion | Already backlogged | — | — | — |
| §34 | Camera-aware quality | LOD by screen size exists | Not tied to shot importance | Let a shot's `Spotlight` raise its subject's LOD floor | S | — | — |
| §35 | Camera transitions | Cut, FadeIn, FadeOut | Crossfade/whip/match cut | Crossfade needs two scene renders; defer with the reasoning already written in `sequence.hpp` | M | Two renders | — |
| §36 | Animation mapping | **Exists** (`clips`) | Retargeting across skeletons | Architect toward; document the limitation | L | — | — |
| §37/§38 | Sockets / attachment | **Exists** | Equipping does not change state | An `equip` action that attaches and sets a property | S | — | §4 |
| §39–§43 | Material/light/particle reaction | **Exists** via routes and reactions | Non-entity nodes cannot carry reactions | Let any node be an entity with reactions and no behaviours — already legal today | S | — | §19 |
| §45/§46 | Editor UI and debug overlays | Panels exist; the world editor is being rebuilt in parallel | No overlays for nav, fields, AI state, shots | Draw into the existing debug channel | M | Editor-only | all |
| §3 (brief) | Fourth-wall cursor / UI events | `comp::LayerStack` can draw it | No scripted cursor or selection event | A cursor is an overlay actor; a click is an event in §17 | S | — | §17 |

---

## 3. The question that has to be settled first

**The engine has two motion authorities and they have opposite determinism properties.**

The sequencer is *pure*: it bakes to tracks, and a frame is `Track::evaluate(t)`. Scrubbing,
replay and offline rendering are correct by construction, and the brief's §22, §23, §26, §27 and
§55 are already satisfied for anything expressed this way.

The entity layer is *stateful*: behaviours integrate, and `IBehavior::reset()` is called on seek —
which means a scrubbed frame today is not the frame you would have reached by playing to that time.
It is the *reset* state. That is honest, and it is fine for ambient motion, but the brief's
acceptance tests (§22 scrubbing, §23 replay, §21 offline matching realtime) demand more than that
from autonomous characters.

Every new system in the matrix inherits this split. A navigating NPC accumulates. A music field
whose source is a *baked actor* is a pure function of time; a field whose source is an *autonomous
character* is not. So the choice is not per system — it is one decision that determines whether the
whole simulation layer is scrub-safe.

Three honest options:

1. **Bake the simulation.** Run it once, record the results as ordinary actor keys, and play those.
   Fully deterministic, matches every existing guarantee, and the director gets exact repeatable
   shots — but the world stops being live in the editor, and "the city continues when the camera
   looks away" becomes a recording.
2. **Fixed-step catch-up with checkpoints.** Simulation runs on its own fixed step, independent of
   render rate; seeking to *t* restores the nearest checkpoint and re-simulates forward. Live and
   deterministic, at the cost of checkpoint memory and a seek that is no longer instant.
3. **Two-tier authority.** Director-owned actors bake (shots stay exact and scrub-safe); ambient
   population simulates live and is explicitly *not* frame-accurate under scrub, which is documented
   rather than hidden. Background NPCs resetting on a scrub is invisible; the protagonist doing so
   is not.

**Recommendation: 3, with 2 available for the ambient tier later.** It matches the brief's own
§9 override hierarchy (Director → Cinematic Action → Behavior → Navigation), it keeps every
existing guarantee exactly where it is today, and it means the protagonist's morning routine — the
part the audience actually watches — is baked and exact, while the crowd is cheap and live. It is
also the only option that does not require finishing a checkpoint system before anything else can
start.

This needs to be an ADR before Phase 3 code lands.

---

## 4. Proposed order

Grouped so that each group can be worked without waiting on the others.

**Group A — the ground truth.** §5 walkable graph, §6 pathfinding, §11 collision. Everything with a
character in it waits on this. Already in progress on `agent/navigation`.

**Group B — intent.** §4 actions, §9 schedules, §10 interactions, §37/§38 equipping, §7 gait
blending. This is what turns the existing `LocomotionState` into a character that can be told to do
something. Depends on A only for the `walkTo` action.

**Group C — reactivity.** §18 trigger volumes, §19 music influence field, §21 the reaction arc, §20
profile library, §39–§43 applied to non-entity nodes. **This is the flagship of both briefs and it
has no dependency on A or B** — a field whose source is a baked actor works today, which makes it
the right place to prove the architecture on Night Shift first.

**Group D — the city.** §13 road graph, §12 vehicles, §30/§31 interiors, §28 population. The largest
group and the least urgent: Night Shift has no traffic and the storyboard's city can be dressed
before it is simulated.

**Group E — direction.** §17 cinematic events, §35 transitions, §34 shot-driven quality, §3's
fourth-wall cursor, §14's shake. Small individually; §17 is the connective tissue the rest assume.

**Night Shift integration** (the addendum) lands on C + E, and should be done as soon as C exists —
it is the acceptance test, and the addendum is right that it is more valuable than a throwaway
scene.

---

## 5. What the briefs assume that is not true

Recorded so the plan is not built on it:

- **"Create a generic cinematic/UI interaction event system if one does not already exist"** — the
  shot/sequence half exists and is mature. Building a second one would be the exact duplication
  §48 forbids.
- **"Introduce the concept of a Shot"** — `seq::Shot` and `app::Shot` both exist, deliberately
  distinct (the piece's shot vs the camera's move). Their naming is already documented.
- **"Create reusable reaction profiles"** — `EntityDesc::profile` is exactly this, including the
  round-trip bookkeeping that keeps a shared profile from being inlined on save.
- **"Support prop sockets"** / **"attachment system"** — both exist.
- **Simulation LOD** — exists for behaviour; the brief's four-band model is one band more than the
  three that ship.
- **§36 retargeting** — the brief allows architecting toward it. The `clips` indirection is that
  architecture; actual cross-skeleton retargeting is a genuine multi-week feature and should not be
  started in this pass.

## 6. What is genuinely missing, in one list

Pathfinding · agent collision · actions · schedules · interactions · trigger volumes · **music
influence fields** · the reaction arc · cinematic events · road graph · vehicles · interiors ·
population management · camera shake · crossfade transitions · nav/AI/field debug overlays.
