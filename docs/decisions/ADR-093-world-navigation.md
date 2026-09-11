# ADR-093: World navigation: a hybrid graph, per-instance obstacles, and a character with somewhere to go

Status: accepted
Date: 2026-09-11

Supersedes nothing. Extends ADR-088 (entities), builds on ADR-080 (camera clearance), ADR-048
(ecology) and ADR-090 (terrain generation and the §3 query surface). Answers §1–§7 of
`docs/world-authoring-spec.md`.

## Context

The brief said the alien "animates in place but cannot meaningfully travel". Half of that had
already been diagnosed and fixed before this work started — `Navigator::sample` rejected any ground
where anything shorter than the walker grew, which in a meadow of 0.7 m grass meant everywhere, and
the `interest` behaviour held the character in `observe` for thirteen seconds at a stretch. With
both corrected it moved on 52% of frames and covered about 195 m a minute.

What remained was the substance of §1–§6, and it was not tuning:

- **Obstacle avoidance did not exist.** `ClearanceField::canopyHeight` (ADR-080) is a *statistical*
  canopy: it reads the biome weights and the scatter layers and answers "trees about fourteen metres
  tall grow around here". It has never been able to answer "there is a trunk at this spot", because
  it never looks at a single placed instance. A walker checking its route against it walks through
  trees, and did.
- **There was no pathfinding.** Navigation was straight-line steering with rejection sampling: pick
  a point in an annulus, try to walk at it, drop it when blocked. That gets a character round a
  boulder. It cannot get one round a lake, and the failure is silent — it looks like a character
  that keeps changing its mind.
- **The behaviour loop was a waypoint debugger.** Pick a random reachable point, walk to it, pause,
  repeat. Nothing about *why* it went anywhere.
- **Grounding was `y = groundHeight(x, z)`**, snapped, every frame, from one point sample.

## Decision

### §2 — Representation: the hybrid, and what each half is for

§2 offered a navmesh, a terrain-aware navigation grid, or a hybrid. This is the hybrid, and the
split is not a compromise — each half answers a question the other cannot.

**Continuous, and already built.** `WorldMap::sample` returns height, normal, slope and water
analytically, anywhere, exactly, from a few noise evaluations. Baking that into a mesh or a grid
would be a second description of the same ground, stale the moment a hill moved and no more accurate
than the function it was baked from. `Navigator` keeps asking the world, as it always did.

**Discrete, and what was missing: reachability.** Answering "can I get there, and which way" needs a
graph. `entity::NavGrid` is a uniform grid over the walkable world — by default 4 m cells, eight
bytes each, 154×154 for Glowmere's 640 m valley — carrying ground height, slope, water, and an
obstruction fraction. A* over it with an octile heuristic produces a corridor, which is then
string-pulled over the grid and handed to the continuous layer to walk.

A navmesh was rejected for a specific reason rather than on taste. Building one means deciding the
walkable set *once*, at bake time, and this world's terrain is regenerated from a seed, its obstacles
come from an ecology pass, and an editor is being built alongside this that moves both. A grid
rebuilds in about 200 ms and is correct by construction; a navmesh would have been a build step
nobody remembered to run.

The grid is deliberately **coarse and soft**. Obstacles contribute a per-cell cost rather than
closing a cell outright, so A* prefers open ground and routes around a thicket while still allowing a
path between two trunks that the fine steering layer will actually thread. A grid fine enough to
resolve a tree trunk would be a hundred times the memory to answer a question the continuous layer
already answers better.

### §5 — Obstacles: cylinders, not bodies

`spatial::ObstacleField` holds `NavigationObstacle { center, radius, base, height, type, blocking }`
— a vertical cylinder — in a uniform grid, and answers four questions: is this disc clear, how far to
the nearest solid, does this segment cross one, and push me out. No bodies, no contacts, no
integration, no callbacks.

Two decisions inside it are load-bearing:

**The height recorded is the height of the *solid*, not of the thing.** A fourteen-metre tree has a
five-metre bounding radius and a half-metre trunk. Recording the bounds would shut the forest to
anything that walks; recording the trunk is cheaper and correct, because the part of a tree a walker
collides with is the part at its own height. A canopy is not an obstacle, it is weather.

**Each obstacle is binned in one cell, and queries pad their search box by the largest radius.**
Binning into every overlapping cell is the obvious thing and it is wrong here: queries are then
handed the same obstacle once per cell, and `resolve` *sums* its overlaps — a body next to one rock
was pushed out of it four times and shot across the valley. Single-cell binning makes every query
duplicate-free by construction rather than by each caller remembering to dedupe.

**Where it lives, and how §3 reaches it.** `spatial::ObstacleField` sits in `spatial/` rather than in
`entity/` or `world/` because both need it — the walker asks
whether it may stand somewhere, and §3's consolidated terrain query surface has to answer
`isOccupied(x, z, radius)` with the same facts — and neither may include the other's headers without
a cycle. `spatial/obstacle_field.hpp` includes glm and the standard library and nothing else.

ADR-090's `TerrainQuery` names a one-method interface, `world::ObstacleField::occupied(p, radius)`,
and says the per-object answer belongs to navigation. `entity::NavigationObstacles` is that answer:
an adapter presenting the container through the interface, held by the composition and attached to
both the navigator and `Composition::terrainQuery()`. It is in `entity/` rather than on the container
because `world` already includes `spatial` (an ecology emits a point cloud), so making the container
implement a `world` interface would close a cycle between the two directories. It also overrides the
optional `penetration`, because the interface's default can only say "blocked" and a steering
behaviour needs a distance to steer by.

The walker does *not* go through that interface for its own queries. `occupied` takes a radius and
nothing else, so it cannot know how tall the mover is or what it can step over; `Navigator` asks
`spatial::ObstacleField` directly with a filter carrying its body radius, its step-over height and
its headroom. The shared interface is the conservative plan-view answer that everything else — the
editor's placement, the water placer, the composer — needs and did not have.

**What becomes an obstacle** is policy, and it lives separately in `entity/obstacles.hpp`. The
judgement §5 asks for — "vegetation and small decorative objects should not necessarily block; large
rocks, structures and cliffs should" — takes code because a scatter layer does not say. Glowmere
grows bushes at 1.1 m, ferns at 1.4 and boulders at 1.6, so height alone cannot tell a rock from a
shrub. The evidence used, in order: `ScatterLayer::category` when the composer set it; the asset's
own path when it did not (Glowmere's scene file predates categories and carries none); the layer's
name as a last resort. And then the *per-instance* scale, which is what makes this per-instance
rather than a per-layer flag — a fan plant at 0.65× is waded through and the same species at 1.6× is
walked around.

A whole layer of ground cover costs one comparison to reject: the tallest instance a layer can grow
is its authored height at its largest scale, so 97,000 grass instances are dismissed without being
iterated. Glowmere's thirteen layers produce **2,355 obstacles** from about 115,000 instances.

### §3 — One set of rules, asked once

ADR-090's `WalkRules` and `TerrainQuery::at()` deliberately reproduced `NavSettings` and
`Navigator::sample` — the same names, the same defaults, the same ladder of bounds, slope, water,
thicket and hero, fired in the same order — and the terrain agent deliberately did not touch
`src/entity/`. That left two copies of the walkability logic with nothing keeping them in step, which
is precisely what §3 exists to prevent.

Resolved in navigation's favour, as §3 asks: `Navigator::sample` is now a *translation* of
`TerrainQuery::at()` rather than a second implementation of it. `NavSettings::walkRules()` hands the
six shared rules to the query, `sample` maps `TerrainPoint` onto `NavSample` and `TerrainReject` onto
`NavReject`, and the only thing navigation still decides for itself is the per-instance obstacle test
— which it must, because that is the one question `TerrainQuery` deliberately does not answer from a
point and a radius.

`NavSettings` keeps three fields `WalkRules` does not have — `stepHeight`, `bodyRadius`, `stepOver`.
Those are properties of a thing that *moves*, and a query about a point has no business knowing them.

### §5 continued — connectivity, and a path request that answers with a reason

A grid of walkable cells does not know that two of them are reachable from each other, and A* finds
that out the hard way: by opening every cell on one side of a divide before concluding there is no
other side. In Glowmere that is 9,500 expansions and 1.3 ms to learn a fact that was settled the
moment the grid was built.

So the walkable set is flood-filled into **connected regions** once, at build time, with *literally*
the same step rule A* uses — one `stepAllowed` function, shared, because a fill more generous than
the search would call two cells connected and then never find a route between them. Glowmere's ground
is 24 regions: one of 21,694 cells and twenty-three pockets totalling 49, which is a fair description
of a valley with some crevices in it. Islands are now explicit, and the build says so in the log when
there is more than one.

On top of that sits the seam an action layer uses, and the reason it exists is §6's "characters
should not freeze forever when their desired destination becomes unavailable". A caller told `false`
cannot avoid freezing, because it has nothing to change. `Navigator::requestPath` returns a
`PathStatus`:

| status | what the caller should do about it |
|---|---|
| `Ok` | walk it |
| `AlreadyThere` | you have arrived; the goal was within tolerance |
| `NoStart` | the mover is somewhere the graph does not recognise — put it back on the ground |
| `NoGoal` | nothing standable near the destination — pick a nearer or different goal |
| `Unreachable` | a different region — pick a goal of a different *kind*, not another one over there |
| `SearchExhausted` | a route may exist; try again, or from somewhere else |
| `NoGraph` | this world has no navigation graph; only a straight line was checked |

`explore` acts on the distinction: an `Unreachable` or `NoGoal` destination is *remembered* as
visited, so the weighted pick stops offering the same island over and over, which is exactly the
freeze §6 names. And `Navigator::pathValid` re-checks a route already in hand without repeating the
search, which is the replanning trigger — a walker checks it on a cadence rather than replanning on
a timer, so a route that is still good is kept and one that an editor dropped a rock across is not.

### §11 — Characters not standing in each other

Separation, not avoidance. Two bodies that each planned around the other would replan every time
anyone walked past, and two that each waited would deadlock facing each other.

`EntityWorld` rebuilds a `spatial::ObstacleField` of `Creature` discs once per update from every
entity that declared a `radius`, and a behaviour asks it for a push. The same uniform grid as the
static obstacles, so separation costs a disc query rather than a pass over every other character —
with three entities that distinction is academic, and building it on N² now would mean rewriting it
when there is a crowd. `radius` defaults to 0 and that means *not a body*, so a craft hovering over a
crowd does not shove it.

It is deliberately not part of the navigator's obstacle set: a route is planned over a world that is
not moving, and who is standing where is a fact about this frame. The push is half-strength and
applied before the static resolve, so making room for someone can never end with a body inside a rock.

### §4 — Grounding: a footprint, not a point

`entity::GroundFollower` reads the ground over the body's own footprint — four points on a circle
plus the centre, plus one ahead scaled by speed — and takes `max(mean, centre)`: on a straight slope
the footprint mean *is* the centre sample, so it follows a hillside exactly; over a dip the mean is
higher and the body bridges it; over a bump the centre is higher and the body stands on it.

The filter is **spatial rather than temporal**, and the first version got this wrong in an
instructive way. A temporal smoother removes jitter by lagging the surface, which is a lag you can
see and which has to be clamped back before it becomes floating — so it spent the whole walk pinned
against its own clamp and came out with vertical acceleration *slightly worse than snapping*. A
spatial filter removes exactly the wrinkles that are smaller than the body standing on them, costs no
lag, and is a pure function of position, so it survives a seek with no history to rebuild.

The clamp band is stated against the footprint and the filtered surface, not against the raw sample:
the body may not sit below the lowest ground its footprint covers, and may not float more than
`maxFloat` above the mean. Clamping to the centre sample instead hands the body every upward wrinkle
by another route, and reduced vertical acceleration by six percent instead of by half.

### §6 — `explore`: a character with a reason

A new behaviour beside `wander` rather than instead of it. `wander` is still right for a background
creature that should mill about near where it was placed; `explore` is a character that crosses a
world on purpose. It runs IDLE → SELECT → NAVIGATE → WALK → ARRIVE → OBSERVE → IDLE, and what keeps
it from reading as a waypoint system is:

- Destinations come from an **interest registry** on `EntityWorld`, assembled from the host's
  landmarks and entities, the luminous patches the lighting pass already reduced each glowing
  population to (ADR-053), and the shoreline and high ground the navigation grid noticed for free
  while it was being built. Glowmere offers **352** of them — 17 landmarks and entities, 96 luminous
  patches, 172 shoreline points and 67 vistas. Choice is weighted by the character's
  own taste per kind, mildly by distance, and suppressed for places recently visited.
- It **plans**, so a lake is walked around rather than discovered, refused and re-rolled.
- Every duration is sampled and every branch is a roll, through the entity's own seeded stream.
- It **arrives**: slows over the last few metres and turns to face what it came for before standing
  still.
- With a settable probability it goes for a walk instead, so it does not shuttle between five named
  places forever.

### §7 — Size is a property of the character

Glowmere's alien is **9.7 m tall** (`alien.gltf` is 120.9 units at a node scale of 0.08). The
world's navigator carries defaults for a person-sized walker — a 0.45 m body radius and 2.2 m of
headroom — and had been navigating this creature with them. `explore` therefore carries `bodyRadius`,
`headroom` and `footprint` as ordinary parameters and keeps its *own copy* of the navigator with its
own size written onto it. A Navigator copy is cheap: the map, the obstacle field and the graph are
all shared. Zero keeps the world's defaults, so a scene that says nothing behaves exactly as before.

Raising `headroom` alone would turn every shrub into a wall — the thicket rule is "too tall to wade
through, too low to walk under", and lifting only its ceiling is the bug that kept the old walker
standing in a meadow. The floor rises with it.

### Determinism, and what a seek now means

`Engine::seekSeconds` reset the modulator, the sources and the music detector, and left every
character exactly where the playhead had walked it to. `EntityWorld::reset()` existed and nothing
called it.

`EntityWorld::seek(time)` resets and re-simulates the behaviour layer at a **fixed step**.
Re-simulation rather than evaluation, because a character that plans a route, steers round a trunk
and is pushed out of a rock has a position that depends on its history; there is no closed form for
"where would it be at t = 94 s", and pretending otherwise would mean throwing away the avoidance that
makes it worth watching.

Two deliberate exclusions. It does not write to the parameter set — the next ordinary frame folds the
offsets on, and writing them here would accumulate onto finals only a real frame clears. And it does
not replay the audio analysis: the bus is null and the parameter set is returned to its authored
values first. Handing the *current* bus and last frame's modulated finals to eighteen hundred
re-simulated steps is not a replay of anything — it applies one instant of the music uniformly across
half a minute, and makes the answer depend on where the playhead happened to be when the seek was
requested, which is the defect. What is promised is that **the same seek time always produces the
same state**. An offline render plays from zero and never seeks, so it is exact either way.

A separate defect found while measuring: an entity skipped by behaviour level of detail before it had
ever run published a default-constructed `LocomotionState` at the world origin, so a character popped
in from (0,0,0) on whichever frame it first came close enough to matter. The first update is now
never skipped.

## Consequences

Measured on `examples/world/glowmere-stylized.json`, over 60 s of its timeline, by reading
`Entity::locomotion()` through the real engine (`tests/integration/test_world_navigation.cpp`):

| | before | after |
|---|---|---|
| distance in a minute | ~195 m | **351 m** |
| ground covered (bounding span) | ~32 m (leashed to a 16 m home radius) | **244 m** |
| frames under way | 52% | **76%** |
| frames inside a solid | not measured; walked through trees | **0 of 3,601** |
| frames in water, or outside the world | not measured | **0 of 3,601** |
| two bodies started 3.5 m inside each other | — | **clear within 1 s, and after** |
| float above the surface | unbounded (snapped) | **≤ 0.32 m** |
| below its own footprint | — | **0** |

(Measured before ADR-090 merged. On the regenerated terrain that came with it — which now actually
has rivers and lakes — the same minute gives 248 m travelled, 175 m of ground covered and 72% of
frames under way. The floors in the test are set well below all of these.)

Costs, stated as numbers:

- `isOccupied` over 2,355 obstacles: **24 ns**.
- A corner-to-corner route across the 640 m valley: **1.3–2 ms**, about 9,500 cells expanded. A
  character asks for one every few seconds. An *unreachable* one costs a region comparison: two
  array lookups.
- Navigation grid build: **~165 ms**, once, at scene load, beside a 50 ms terrain build and about a
  second of glTF decoding. 23,716 cells at 190 KB. Dominated by two full `WorldMap` evaluations per
  cell — one for the ground and one inside `canopyHeightAt` — which is where to look if it ever
  needs to be cheaper.
- Behaviour level of detail: the walker runs at a tenth of the frame rate beyond 90 m and covers the
  same ground; over the minute it ticked on about a quarter of the frames.

## Rejected alternatives

**A navmesh.** Bake-time walkability in a world whose terrain, ecology and obstacles are all being
made editable. See above.

**A general-purpose navigation framework (Recast/Detour or equivalent).** §2 says not to, and the
reason holds: the whole discrete layer here is 400 lines, rebuilds from the world it describes, and
has no concept this codebase does not already have.

**Heavyweight rigid bodies for obstacles.** §5 says not to. Nothing here needs contact resolution,
restitution or a solver; it needs to know where the trunks are.

**Extending `wander` instead of adding `explore`.** `wander` has a clear and different job, and two
Glowmere scenes already use it. Widening it into a state machine would have made one behaviour that
does both jobs badly.

**Making behaviour a pure function of the clock.** It would make scrubbing exact, and it is
incompatible with path following and obstacle avoidance, which are the reason the character is worth
watching. Fixed-step re-simulation buys the property that was actually missing.

## Revisit triggers

- **Water as an interest.** ADR-090 brought `world::WaterCourse` with a centreline, a downstream
  `flowAt` and `contains`. The interest registry currently derives its shoreline points from the
  navigation grid — a walkable cell next to a wet one — which is cheap and correct but knows nothing
  about which body of water it is on. A character that should follow a river downstream, or walk to
  a named lake, wants the water courses themselves.
- **A second character joins the scene.** Per-character nav settings exist on `explore`; the
  navigator's own `NavSettings` are still world-level and used by `pickDestination` and the grid
  build. If characters of very different sizes need different *graphs*, the grid needs a size key.
- **Dynamic obstacles.** The field is rebuilt per composition rebuild and is otherwise static. A
  character that should avoid another character needs `ObstacleType::Creature` entries refreshed per
  frame, which the container supports and nothing currently does.
- **Worlds much larger than 640 m.** The grid is capped at two million cells and warns rather than
  allocating; beyond that it wants tiling, or a coarser cell with a finer local one.
