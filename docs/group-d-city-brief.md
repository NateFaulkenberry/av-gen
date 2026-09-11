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

---

# PROGRESS — update this section as you go

**This brief is live.** It is written to be picked up mid-way, so whoever is working on it keeps this
section current: what is done, what is half-done, and what the next step is. A handoff that says
"see the commits" is not a handoff.

## Done

**ADR-100 — what a layout emits.** The question the brief said to answer first, answered:
`docs/decisions/ADR-100-city-layout.md`. The repeated fabric of the city (roads, pavements, ordinary
buildings, props) is emitted as `spatial::PointCloud`s and installed exactly as a scatter layer,
because `ProceduralGeometry::distribution.scatterCloud` already takes explicit positions and
everything past that point is the instanced path Glowmere's 150,000 instances already travel — GPU
culling, the LOD ladder, wind. The handful of pieces a director names — the protagonist's building,
the stage — are ordinary composition nodes. The ADR records why neither alone works.

**The plan — `src/world/city.hpp` / `city.cpp`.** A lattice of cells, each with a kind (road,
junction, crossing, pavement, plot, plaza) and a quarter turn, as a pure function of `CitySettings`.
The street network is laid first and blocks fill what is left, because roads carved out of a field of
plots end up with plots hanging over them wherever the arithmetic is off by one. Planning knows
nothing about assets on purpose: the structural properties can then be checked exhaustively with no
library, device or mesh.

Tests: `tests/unit/test_city.cpp`, tag `[city]`. Ten cases, 1,157 assertions. Full suite **1550,
green**.

Two things the tests caught, worth knowing because both would have rendered as a plausible-looking
grid:

- **A block two cells across is all edge.** Every cell touches a road, so the pavement ring consumes
  the whole block and the city has roads, footways and *no buildings*. It counted fine and rendered
  as an empty grid. `blockCells` now has a floor of 3 and the refusal says why.
- The road-continuity test's own first version inferred road columns from row 0, which is itself a
  road — so it called every column a road and passed vacuously. It reads from a line crossing the
  blocks now. Worth remembering when writing the next structural test.

**The placer — `src/world/city_place.cpp`, `assets/city-pieces.manifest.json`.** A plan plus an
asset library becomes `PlacedCity`: one `spatial::PointCloud` per asset, which is the second half of
ADR-100 and the thing `distribution.scatterCloud` takes. Roles come from manifest **tags** — an entry
tagged `road` dresses road cells — so adding a road variant is a line of JSON, not a code change.

`assets/city-pieces.manifest.json` is deliberately a *second* manifest, separate from
`assets/city.manifest.json`. That one is the **scatter** library the ecology composer reads and is
capped at 64 layers; this one is the **tiling** library the city placer reads and has no such cap,
because `AssetLibrary` never had one — the cap is `src/world/ecology.cpp:221` and belongs to the
ecology alone. Only pieces that tile edge to edge belong here: a curve or a bend does not, because
the lattice knows straights and crossroads and nothing else.

Tests: tag `[city][place]`. Full suite **1555, green**.

**The design finding worth keeping.** The placer first scaled each piece by its own bounding box,
which sounds like the thing that would make everything fit and does the opposite. `road-side` bounds
1.0 x 1.31 because its kerb is *meant* to overhang the tile; scaling by that box shrank the tile it
fills to 6.1 m of an 8 m cell and left a gap beside every one of them. **A tiling pack has to declare
its tile size — it cannot be recovered from the mesh**, because decoration that reaches past the tile
is the artist's intent. `CitySettings::tileUnits` states it (1.0 for Kenney) and the whole pack scales
uniformly.

**The node that installs one — `NodeKind::City`.** A `city` node carries `CitySettings` and a path
to the tiling manifest, and `Composition::rebuild` plans, places and emits one instanced
`ProceduralGeometry` per piece. The node carries the *description*, never the placements, because a
scatter cloud is a runtime `shared_ptr` and is not serialised — exactly the arrangement the terrain
node uses for its ecology, and for the same reason: the settings survive a save and the placements
are made again from them. Round-trip tested.

**`examples/city/first-block.scene.json`** is the first one, with `examples/lightrigs/city-dawn.rig.json`.
It renders: **13x13 cells, 7 pieces, 152 instances, 8,734 triangles, 0 GPU errors.**

Tests: tags `[city]`, `[city][place]`, `[city][node]`. Full suite **1558, green**.

Three things that cost time and would cost it again:

- **Naming the asset is not resolving it.** Setting `source.asset` and nothing else produced a
  composition that reported 152 instances placed and rendered an empty frame. The registry has to
  load the scene and the parts have to be attached — `resolveMeshParts` then `source.assetMesh`, the
  way the ecology path does. A test now asserts `assetMesh != nullptr` for exactly this.
- **A scene with no light renders the city at (10,18,41) against a (37,37,51) background** — present,
  correct and invisible. The first frame needs a rig; the example carries a dawn one.
- **`Composition::loadFile` does not build anything.** `update()` is what flattens nodes into a
  scene, and `AssetRegistry::setBaseDirectory` has to be set first or relative paths resolve
  somewhere else and the rebuild warns into a log nobody reads. Both bit this work's own tests.

**Buildings — families, facing and two scale rules.** Plots are built on. 26 building pieces from
the Kenney commercial, suburban and industrial kits are tagged in the manifest, and the example now
renders **19x19 cells, 28 pieces, 425 instances, 0 GPU errors** as a city rather than a road layout.
Four decisions, each recorded in ADR-100:

- **A block builds from one family**, chosen from the seed and the block's coordinates. Drawing each
  plot from the whole library independently puts a bungalow between two towers on every block, which
  is the characteristic look of a city nobody planned. The family comes from a `family:<name>` tag —
  a *prefix*, because the code has to tell a family from any other word an artist writes, and "a tag
  only some of them carry" silently reclassifies the vocabulary the moment somebody tags one
  `damaged`.
- **A building faces the nearest street.** Decided in `planCity`, not the placer, because it needs
  only the lattice — so it is checked exhaustively with no asset, mesh or device.
- **Two scale rules.** Ground pieces take the pack-wide tile scale (`tileUnits`, above). A building
  is scaled to *its plot* instead: it has no tile to meet a neighbour across, and its footprint is
  its own — the same kit ranges 0.9 to 1.3 units — so the pack scale would leave some overhanging
  the footway and others adrift in the middle of the plot. `CitySettings::plotFill` (0.9) is the
  fraction of the plot it fills.
- **A plot gets ground *and* a building.** Every other cell is its own floor — a road tile *is* the
  road — but a building is a solid thing standing on something, and a plot given only a building has
  the void around its feet. Visible in the frame immediately.

Four things found by doing it, all of which would have shipped as "the city looks a bit wrong":

- **A block 5 cells across has a landlocked plot in the middle** — no street frontage in any
  direction, so its front door opens onto the back of the building in front of it. Found by the
  facing test. The block's core now builds on its *perimeter* and what is left becomes
  `CellKind::Courtyard`: the back of the block, where the bins and the parking are. A perimeter
  block, which is what real ones are.
- **Six of the building pieces are modelled about a corner, not their middle.**
  `industrial-building-h` spans -0.58 units to one side, which at plot scale is **4.6 m** — half the
  building in the road. `AssetDescriptor::naturalCentre` now records the bounds centre and buildings
  are recentred on their plot. Note `road-side` is off-centre too and is deliberately *not*
  recentred: that is its kerb overhanging the tile on purpose. Same measurement, opposite meaning.
- **`environment.stylized` discards base-colour texture RGB.** This is engine behaviour, not a bug
  (`shaders/pbr_shade.wgsl`, gated on `frame.lightCounts.z`), and it is why every Kenney asset
  rendered bone white for several passes. **Any palette-textured import needs `"stylized": false`.**
  Worth knowing well beyond the city: it applies to all 1,071 imported meshes.
- **Camera `"mode": 0` is orbit and ignores `position`/`target`** — mode 1 is the free camera. Two
  renders came back pixel-identical after retuning the camera before this was spotted.

A test that hardcoded the example's `blockCells` broke the moment the example was retuned. It reads
the value from the file now: an example is art direction, and a test that pins one of its numbers
teaches people to edit the test rather than read it.

Tests: 22 cases under `[city]`, 2,540 assertions.

**Dressing — family ground, yards and a street that steps.** The three items the previous PROGRESS
named, done. The example renders **19x19 cells, 32 pieces, 481 instances, 0 GPU errors**.

- **Ground belongs to a family.** A suburban block stands on grass (`ground_grass`, a 1x1 two-triangle
  tile from the nature kit), commercial and industrial on paving. `CityLibrary` now groups three
  roles by family — buildings, ground and props — because all three are things a block's character
  governs. Roads and pavements deliberately are not: a street is the same street whichever block it
  runs past, and giving each family its own kerb is how a city stops joining up.
- **A yard has several things in it.** Props are the first thing in this lattice that is not one
  piece per cell: `propsPerCell` (default 3) scatters trees, planters, bushes, tanks and solar panels
  across courtyards and plazas, jittered within `propSpread` of the centre so nothing strays into the
  road, and turned freely — a prop adjoins nothing, so the quarter-turn rule that keeps roads meeting
  does not apply, and a row of trees all facing one way is the giveaway that a program placed them.
  The count varies per cell too; exactly three things in every yard is a pattern rather than a yard.
- **A street steps rather than jumps.** A block now gets a height *band* as well as a family: the
  family's pieces are sorted by height-over-footprint, the band names a place in that order, and each
  plot lands within one piece of it. Neighbouring buildings differ by a storey instead of by a tower,
  and two blocks of the same family still differ from each other.

Props take **pack scale**, like the ground, not the plot rule — a tree has no plot to fill, and
stretching one to an 8 m footprint is how a garden ends up with a single enormous shrub. Curated
rather than swept: shipping containers measure 24 m long at pack scale, and parasols are shopfront
details that read as a 6 m umbrella when free-standing.

**A test that passed against a deliberately broken placer.** Worth reading before writing the next
one. "A street steps rather than jumps" first compared a block's height spread against the *whole
city's*, and passed with the height band disabled — because the city spans three families (0.57 to
3.15 aspect) and any one block draws from one of them, so the ratio held on the strength of the
family rule alone, which was already working. It measured the wrong baseline. It now compares each
block against **its own family's** range, and fails the broken placer at 0.86 where the real one
scores under 0.6. Every new check here was negative-controlled by breaking the code it tests; two of
them needed rewriting when the control passed.

## Next — the road graph, and what dressing is still thin

1. **Commercial and industrial ground is dark** (`tile-low`, plain asphalt) and reads as a hole from a
   low angle, where the suburban grass reads correctly. This is art direction, not a defect: the kit
   has no light forecourt tile. A recoloured variant or a different piece would fix it.
2. **Pavements have nothing on them** — no lamp posts, bins, benches, bus stops. `city-kit-roads` has
   95 pieces, most untagged, and the prop scatter that yards use would work on a footway unchanged.
3. **Blocks are one family throughout.** Real cities have a corner shop in a residential street. The
   family could be a weighting rather than a hard pick.

## Not started

The road graph (§2 of this brief) and interiors (§3). Neither has been designed beyond what is
written above; both are still exactly as the brief describes them.

One thing §2 gets for free when it starts: the plan already distinguishes `Road`, `Junction`,
`Crossing` and `Pavement`, and `CityPlan::isCarriageway` exists. A lane graph is derivable from the
lattice rather than authored a second time, which is what the brief asks for.
