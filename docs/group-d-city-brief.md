# Group D — the city as a place: interiors, a road graph, and a street that is laid out

A brief for one agent. Everything else in the *All You Got* architecture has landed; these three are
what remains, and they are the three that no amount of tool work reaches because **the engine has no
concept of them**.

Read `docs/cinematic-world-gap-analysis.md` first. It is the audit that named this group, and its
matrix rows §12, §13, §30 and §31 are the requirements below. Do not re-audit — it is current.

## The one sentence that explains all three

This engine places things in exactly two ways: **an author puts a node somewhere**, or **the composer
scatters by density**. A forest is a density. A street is not. There is no third producer, and these
three gaps are all the shape of that hole.

- **A street grid** is a *tiling* problem. `world::composeWorld` emits `ScatterLayer`s — a rule saying
  "this asset, this category, these biomes, this slope range, this density" — and the terrain build
  turns each into instances. Nothing in that pipeline can say "a road tile here, rotated, and the
  next one adjoining it". `src/world/ecology.cpp:221` also caps a world at **64 scatter layers**,
  which is why the shipped `assets/city.manifest.json` lists 47 curated entries and not the 1,071
  meshes on disk.
- **A road graph** is the same absence one level up: lanes, intersections, crossings and stops are
  not types. `entity::NavGrid` (ADR-093) can path a *walker* over open ground, and that is all.
- **Interiors** do not exist as a relationship. A building is a mesh. There is no entrance, no room,
  no inside.

## What already exists — build on it, do not rebuild it

The audit's standing rule applies: if a system can do something, use it.

| You will need | It already exists as | Where |
|---|---|---|
| Where the ground is, how steep, whether wet | `world::TerrainQuery` | `src/world/terrain_query.hpp`, ADR-090 |
| Per-instance occupancy ("is something standing here") | `world::ObstacleField`, implemented by `entity::NavigationObstacles` | `src/world/terrain_query.hpp`, `src/entity/obstacles.hpp`, ADR-093 |
| Pathfinding for people | `entity::NavGrid::path(PathRequest)` → `PathResult` with a typed failure | `src/entity/nav_grid.hpp`, ADR-093 |
| Characters with intent, schedules, interactions | `entity::ActionQueue`, `Schedule`, `InteractionDesc` | `src/entity/action.hpp`, ADR-096 |
| Cars and lights answering the music | `entity::FieldDesc` scaling existing reactions | `src/entity/field.hpp`, ADR-097 |
| Cutting between places | `seq::SceneSlot` — visibility on a composition node, not a file load | `src/seq/sequence.hpp`, ADR-089 |
| A camera that enters something | `app::ShotKind::Entry` — "travel into the interior of something" | `src/app/cinematic.hpp:38`, ADR-062 |
| Nodes, groups, parenting, undo | `scene::Composition::addNode/removeNode/detachNode`, `ui::EditHistory` | `src/scene/composition.hpp`, ADR-092 |
| Making objects from a prompt | `scene.create_node`, `world.generate`, `render.probe` | `src/ai/engine_tools.cpp`, ADR-094, `docs/ai-tool-backlog.md` |

**The assets are already imported and every one is verified to load.** 866 Kenney GLB (roads,
commercial, industrial, suburban, modular buildings, cars, **furniture**, mini-characters rigged with
64 clips each, nature), 153 Quaternius downtown modules, 25 converted street pieces, 18 characters on
66-joint rigs with a 43-clip animation library, a concert stage and instruments. Provenance and
licences: `assets/imported/ATTRIBUTION.md`. Curation and art direction: `assets/city.manifest.json`.

**Mind the scale.** Kenney's kit is modelled on a 1×1 unit tile with a 0.79-unit person; Quaternius'
downtown reaches 28 units. Neither is metres. The manifest's `preferredScale` states the height in
metres an instance wants, and reconciling the packs is done there — in art direction — never by
editing geometry. A street grid must adopt the same discipline: **one module size in metres**, and
every pack scaled to it.

---

## 1. The street grid

### What to build

A placement producer that is not a scatter: something that lays modular pieces on a lattice with
rotation and adjacency. Roads that join, pavements that follow them, buildings set back from them,
props along them.

### The architectural question to answer first

**Where does it live, and what does it emit?** The precedent is `world::composeWorld` →
`app::GeneratedWorld` → `app::installWorld` (`src/app/world_builder.hpp`). A city layout is the same
shape of thing — a pure function from a description to a set of placements — and it should be
installable the same way, so that the editor's Generate button, the `--generate` flag and the
assistant's `world.generate` tool all reach it without three code paths.

Decide and write it down: does a layout emit **composition nodes** (one per placed piece, which the
world editor can then select and move, and which the project saves), or a new kind of **instanced
layer** (cheap, but not individually addressable)? A miniature city that a director will hand-dress
argues for nodes; a city of ten thousand pieces argues for instances. Measure before choosing —
`docs/world-performance.md` has the method and `docs/renderer-2-backlog.md` records what has already
been tried and rejected *with numbers*.

### Constraints that are already true

- **The 64-layer cap** (`src/world/ecology.cpp:221`) is not a bug to raise. It exists because the
  composer builds one layer per asset. A city is not 900 scatter layers; it is a lattice with a
  library behind it.
- **Determinism (ADR-091).** Same seed, same city, every time. A layout must be a pure function of
  its inputs, like `world::composeWorld` is.
- `TerrainQuery` decides where ground is. Do not implement height or slope logic — §3 of the audit
  is explicit that there is one answer to "where is the ground" and it is that.

### Done when

A recipe or a layout description produces a city block that reads as one: roads that join without
gaps, pavements at their edges, buildings set back and not intersecting, props and trees along the
street, nothing floating and nothing inside anything else. `render.probe` can confirm the camera sees
it; a rendered frame is the real check.

---

## 2. The road graph, and vehicles on it

### What to build

`city::Graph` or similar: **lanes, intersections, crossings, pavements, entrances, transit stops** as
types, derived from the layout of §1 rather than authored twice. Then a lightweight traffic system on
it — position along a lane, speed, the car in front, the light ahead.

### The seams

- Pedestrians already path with `entity::NavGrid`. A pavement is a *hint* to that grid, not a second
  navigator: prefer making pavement cells cheap and road cells expensive (`NavPathCost` exists) over
  building a parallel pedestrian router.
- Vehicles are **not** walkers and should not use the walker grid. A lane is a spline with a
  direction; a car is a position along it. `spatial::Spline` is arc-length parameterised and already
  used for actor paths (`seq::ActorPath`).
- **ADR-091 decides how they behave.** Vehicles are the *live* tier: ambient, stateful, explicitly not
  frame-accurate under scrub. A director-owned car in a shot is the *baked* tier and goes on an
  actor's keys. Do not make every car baked, and do not make the hero car live.
- Headlights and signage answering the music is `entity::FieldDesc` (ADR-097) — a reaction whose
  depth a field scales. Do not add a second reactivity path; §20 of the original brief forbids it and
  the field system is finished.

### Done when

Cars travel lanes without entering buildings or pavements, stop at lights, do not pile into each
other, and do not pop in or out within sight of the camera. Their count is a parameter, and the
frame cost of the whole traffic system is measured and recorded.

---

## 3. Interiors

### What to build

A building that has an inside: `entrances`, `rooms`, and an interior the camera can enter and a
character can walk into.

### The design the engine is already pointing at

**Do not build streaming.** `seq::SceneSlot` switches scenes by toggling visibility on a composition
node, and a `kind: "scene"` node already loads a child scene file and propagates visibility through
its whole subtree. An interior is therefore a *scene node inside a building node*, hidden until a
shot wants it — which costs a bool, is scrub-safe, and is the trade ADR-089 already chose and
documented for scene switching.

The brief's own tiering is the right shape and should become data on a building node:

```
exterior only  ·  exterior + facade interior  ·  fully explorable  ·  cinematic interior
```

The camera side already exists: `ShotKind::Entry` is "travel into the interior of something". The
part that does not exist is the building knowing where its door and its window are, so a shot can be
aimed at them by name rather than by hand-typed coordinates.

### Watch for

- **Navigation must follow the character inside.** `entity::NavGrid` is built over the terrain; an
  interior floor is not terrain. Decide whether an interior gets its own small grid linked by the
  entrance, or whether the floor registers as walkable in the main one. ADR-093 §1 explains why the
  continuous layer stayed analytic — read it before choosing.
- **140 Kenney furniture meshes are already on disk** (`assets/kenney/city/furniture-kit/`) and are
  not in the scatter manifest on purpose: furniture is placed, not scattered.

### Done when

A camera can start outside a building, enter through a named window or door, and find a dressed room
with a character in it, in one continuous shot — and the interior costs nothing when no shot is
inside it.

---

## Rules

These are the repository's standing rules, and they are not decoration — every one of them is written
because it was violated once.

1. **Extend, do not duplicate.** Before building anything, find what already does it. The table above
   is a starting point, not the whole list.
2. **Measure, do not assume.** Best-of-N under contention, never a median. State the method with the
   number. `tools/render_bench.py` exists; `docs/world-performance.md` has the shape.
3. **Negative-control every test that guards a defect class.** Prove it fails against the unfixed
   code. A test that has never failed has not been shown to test anything.
4. **Do not weaken a test to fit new behaviour.** If one fails, find out whether the test or the code
   is wrong and say which.
5. **Determinism (ADR-091).** Seeded streams, no wall-clock, no frame-rate-dependent integration.
   Say which tier each new moving thing is in.
6. **Nothing scene-specific.** No `if (scene == "AllYouGot")`. Configuration lives in scene data.
7. **Write the ADR.** Three decisions here are significant enough to need one: what a layout emits,
   how vehicles are represented, and how an interior is attached to a building. Next free number is
   **100** — check `docs/decisions/README.md` when you write it, because numbers have collided
   before when several agents wrote at once.
8. **Build and test**: `cmake --build build/release -j8`, then `ctest -j2` — not `-j4`, because GPU
   timing tests fail under contention. **1540 tests pass on `main` today; that number must not go
   down.**

## Documentation

**Read before starting**

- `docs/cinematic-world-gap-analysis.md` — the audit that defines this group, and what already exists
- `docs/decisions/ADR-091-simulation-authority.md` — what bakes, what lives, what a scrub may do
- `docs/decisions/ADR-093-world-navigation.md` — the navigation model and why it is a hybrid
- `docs/world.md` — the world system as it stands
- `docs/asset-library.md` and `assets/city.manifest.json` — what may be placed, and at what scale

**Read when you reach that part**

- `docs/decisions/ADR-090-terrain-generation-and-queries.md` — the one place to ask where the ground is
- `docs/decisions/ADR-089-the-cinematic-sequence.md` — scene slots, shots, and why the sequence bakes
- `docs/decisions/ADR-092-the-world-editor.md` — placement, undo, and the ghost under the cursor
- `docs/decisions/ADR-096-actions-and-intent.md` — characters with somewhere to be
- `docs/decisions/ADR-097-spatial-reactivity.md` — music influence fields
- `docs/decisions/ADR-094-ai-control-plane.md` and `docs/ai-tool-backlog.md` — the 54 tools, and what
  it took to make each one honest
- `docs/world-authoring-spec.md` — the original authoring brief
- `docs/world-performance.md`, `docs/renderer-2-backlog.md` — how to measure, and what has already
  been rejected with numbers
- `docs/assets.md`, `assets/imported/ATTRIBUTION.md` — the asset pipeline and every pack's licence
- `docs/testing.md` — the testing conventions, including the GPU contention rule

## Report when done

What you built; what you reused rather than rebuilt; the three ADR decisions and why; measured
performance with the method stated; tests added and their results; and — the section that matters
most — **what remains unfinished or hardcoded, honestly.** A crude city that is genuinely laid out
beats a beautiful one built on a one-off script, and an honest list of what is missing is worth more
than a claim that nothing is.
