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

**A road tile has to have a road on it.** The correction that mattered most here, and a correction
of this document. The previous note blamed "commercial and industrial ground is dark". That was
wrong. `road-straight-barrier` was tagged `road`: 48 vertices, every one at |x| >= 0.45 — two rails
meant to run *along* a carriageway, with no carriageway of its own. Tagged as a road it was drawn for
about half the road cells, and each rendered as a hole straight through to the background.

Nothing in the system could see it. `bare = 0`: a piece was placed on all 361 cells. `placeCity`
counts pieces and cannot know whether a piece has a surface — and neither can a bounding box, since
the rails span the full 1x1 tile, so the box is exactly the box of a road.

What found it was measuring instead of guessing, in three steps worth repeating:

1. **Sample the albedo.** Reading each piece's UVs against its palette put `tile-low` at luma 168.7 —
   the *lightest* ground in the pack. The "dark ground" theory died there.
2. **Raise the sun.** The dark rectangles survived a 55-degree sun, so they were not shadows.
3. **Render one asset at a time.** `road-straight` alone covers the map completely;
   `road-straight-barrier` alone leaves the entire road grid black. No ambiguity left.

A test now asserts the invariant that was only ever implied: a piece tagged for a ground role must
have triangles under five points spread across its tile — not the corners, where a piece may stop
short to let a neighbour's kerb overhang. The piece is retagged `roadside` rather than dropped:
rails alongside a carriageway are real, and need the layered placement that plots already use.

**Street furniture.** Lamps, a traffic signal and signs stand on footways. Placed at the **kerb**,
not jittered like a yard prop — a lamp post in the middle of a footway is in the way, one at the back
of it is in a garden — and facing the carriageway, which is the point of a signal and the right way
round for a lamp's arm. `streetPropChance` (0.22) keeps them sparse: at one module per cell,
furnishing every pavement is a lamp every 8 m, about three times the real spacing. A pavement cell
with no carriageway beside it is inside a block and gets nothing.

**The corner shop.** `cornerMix` (0.5) lets a plot on a block's corner build from a family other than
its block's. Corners only: that is where two streets meet and where a shop would actually stand, and
a bungalow dropped mid-terrace is not variety but a mistake. Which other family comes from the seed,
so no family is named in code — "commercial" is a word in a manifest, not a concept `city_place.cpp`
knows. A corner's ground goes with its trade, since a shop on a lawn is the odder result.

`CityPlacement::prop` now distinguishes a thing standing *on* a cell from the cell itself. Three
tests were conflating them, and a lamp post is not a second pavement.

**Three tests had to exclude corners, so a fourth now proves corners trade** — otherwise those
exclusions would quietly remove the coverage they look like they preserve. Both new rules were
negative-controlled: disabling the corner draw fails at "0 of 64 corners trade", and placing
furniture at the cell centre instead of the kerb fails its offset check.

**Layered decoration, done as the kit intends it.** Classifying all 95 road pieces by the tile-
coverage check written for the last bug turned up the pattern: the kit is built in **pairs**.
`road-straight` + `road-straight-barrier`, `road-crossroad` + `road-crossroad-barrier`, `road-square`
+ `road-square-barrier`, `road-side` + `road-side-barrier` — 28 pieces cover no part of their tile
and 32 cover all of it, and every one of the former is a rail drawn to sit on one of the latter.

So an overlay is not "decoration near a road": it **names the surface it belongs to**
(`overlays:road-straight`) and is placed with that surface's transform exactly — same cell, same
quarter turn, same scale. Anything else puts a guard rail through the middle of the road it is meant
to edge. Named explicitly rather than inferred from the `-barrier` suffix, because a naming
convention is a coincidence the day someone adds a piece that breaks it, and this one already has
exceptions (`road-straight-barrier-half` belongs to `road-straight` too).

Decided per **run** of three cells, not per cell: a rail on one cell of an otherwise open road is not
a rail, it is litter. `road-straight-barrier` — the piece that punched holes through every render
until it was untagged — is now back in the city, correctly.

**`buildDepth`, because the band was always one cell.** A block's buildings occupied the core's
perimeter regardless of block size, so the yard grew with the square of the block: at `blockCells` 9
a block is a ring of houses round a field, which a render made obvious immediately. The band is now
`buildDepth` cells deep, clamped to what the core holds.

It trades something, and the test says so rather than hiding it. At depth one every plot has clear
frontage — "Every building faces a street" proves a plot reaches a road without passing through
another building. Deeper, the inner rows cannot: they still face the nearest street, but through
their neighbours, the way a mews does. A real arrangement, not a defect, but not the guarantee depth
one gives, so nothing downstream should assume it.

**The pavement ring: measured, not guessed at, and it needs a decision.** The ring is one cell — 8 m
at the default module — and a cell lattice cannot hold a 2.5 m footway. Two ways out, and the kit
points at the second:

1. **Bigger blocks.** The ring is one cell whatever the block, so its share falls: 64% of a block at
   `blockCells` 5, 49% at 7, 39% at 9. Free, and `buildDepth` now makes big blocks worth having.
2. **Let the road carry its own footway.** `road-side` measures 1.0 x 1.31 — a road tile with a
   0.31-unit kerb overhanging, which is **2.5 m at an 8 m module**: exactly a footway. Used as the
   kit intends, with `roadCells` 2 so each band has an outward-facing edge lane each way, a street
   becomes 2 lanes + 2 footways and the block's pavement ring disappears entirely, giving those
   cells back to buildings.

Option 2 is the right answer and is not a small change: `CellKind::Pavement` stops being a block's
edge ring, and "A building never stands in the road, and never flush against one" has to be restated
— a building would be flush against the kerb, which is what a street is. Worth doing deliberately
rather than as a side effect, so it is written down here rather than half-started.

**The road-carried footway, built.** A block used to spend a whole cell on its pavement ring — 8 m
at the default module, where a footway wants about 2.5 — so footways read as plazas. The fix is not
a finer lattice: a street-facing cell is **shared**. Its ground is the footway tile as before, and a
building stands on the part the footway does not need, set back from the kerb. That is what a street
is. `footwayMetres` defaults to 2.48, which is `road-side`'s own measurement: that piece is
`road-straight` with its kerb pushed out 0.31 units on one side, and 0.31 of an 8 m module is 2.48.

Setting it to 0 restores the ring, which an alley wants and which every scene written before this
expects. Both arrangements are tested.

The example goes from **589 to 752 instances** on the same lattice, and reads as a dense city rather
than buildings scattered in a grey field.

**It inverted an invariant, exactly as predicted, and six tests had to be restated.** The old one —
"A building never stands in the road, and never flush against one" — was correct while a block spent
a ring on pavement and is wrong now: a frontage plot *does* touch the carriageway, because it carries
the footway and the building on it is set back behind that. What survives is the half that matters
(no building ever stands *in* a road) plus a new half (a plot beside a carriageway must be marked
frontage, since that flag is what sets the building back; an unmarked one would be a house in the
traffic). The old rule is kept as a negative control under `footwayMetres = 0`.

"Every carriageway has a pavement beside it" became "…somewhere to walk beside it", because a footway
is no longer always a cell of its own. The property was about walkable ground; it asks for that now
instead of a cell kind, and runs under both arrangements.

**And a whole class of test fragility is gone.** `CityPlacement::cells` records the cell each instance
came from. Tests had been inferring it by rounding the position — which was already wrong for the
widest buildings once they were shifted by their own off-centre pivot *and* set back from the kerb,
and showed up as "block 1,1 has 2 families" when the real cause was a corner building attributed to
its neighbour. Anything reasoning per cell — the lane graph, next — should read this rather than
re-derive it.

## Next

1. **The road graph** (§2) and **interiors** (§3), still as the brief describes them. §2 now gets two
   things free: the plan distinguishes `Road`, `Junction`, `Crossing` and `Pavement` with
   `isCarriageway`, and `CityPlacement::cells` maps every placed piece back to its cell.

## Not started

The road graph (§2 of this brief) and interiors (§3). Neither has been designed beyond what is
written above; both are still exactly as the brief describes them.

One thing §2 gets for free when it starts: the plan already distinguishes `Road`, `Junction`,
`Crossing` and `Pavement`, and `CityPlan::isCarriageway` exists. A lane graph is derivable from the
lattice rather than authored a second time, which is what the brief asks for.
