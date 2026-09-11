# ADR-100: A city is a lattice, and it emits the clouds the scatter path already draws

**Status:** accepted
**Date:** 2026-09-11
**Context:** Group D of `docs/cinematic-world-gap-analysis.md`; the brief in `docs/group-d-city-brief.md`

## The problem

This engine places things in exactly two ways. An author puts a node somewhere, or
`world::composeWorld` scatters by density — a `ScatterLayer` saying "this asset, these biomes, this
slope range, this many per square metre", which the terrain build turns into instances.

A forest is a density. **A street is not.** Nothing in either path can say "a road tile here, rotated
a quarter turn, and the next one adjoining it", and `src/world/ecology.cpp:221` caps a world at 64
scatter layers besides. So a city had no producer, and the three Group D gaps — a street grid, a road
graph, interiors — are all the shape of that one hole.

## The question this decides

The brief asked it directly: **what does a layout emit?** Composition nodes, which the editor can
select and the project saves, one per placed piece — or instanced layers, which are cheap and
anonymous?

## Decision

**Both, split by whether a piece needs a name.**

The repeated fabric of a city — road tiles, pavements, the ordinary buildings, street props — is
emitted as **`spatial::PointCloud`s, one per asset**, and installed exactly as a scatter layer is.
`scene::ProceduralGeometry::distribution.scatterCloud` already takes a cloud of explicit positions,
rotations and scales, and everything downstream of that point is the path Glowmere's 150,000
instances already travel: GPU culling, the LOD ladder, the wind and chroma systems, instanced draws.
A city that emitted anything else would be reimplementing all of it.

The handful of pieces a director addresses by name — the protagonist's building, the concert stage,
a landmark — are emitted as **ordinary composition nodes** through `Composition::addNode`, which the
world editor can already select, move and undo, the project already saves, and `scene.create_node`
already makes.

## Why not one or the other

**All nodes.** A miniature city block is a few hundred pieces and a city is thousands. Every node
registers its own transform parameters — `night-shift.json` already carries 19,171 parameters for a
scene of two blocks — and every node is flattened on every structural edit. The editor measured a
flatten at 22–33 ms *with* the terrain cache; adding a few thousand nodes to that is the responsive
editor gone, and none of it buys anything for a kerbstone nobody will ever select.

**All instances.** Then nothing in the city has a name. The storyboard's first four shots are about
one specific building and one specific window; a camera cannot be aimed at instance 4,312 of
`building-a`, a character cannot be told to walk into it, and an interior has nothing to attach to.

The split is not a compromise between the two. It is the same distinction the engine already makes
everywhere else: a scatter layer is weather, a node is a thing.

## The layout itself

A **lattice**. `world::CityPlan` is a grid of cells, each carrying a kind (road, junction, pavement,
building plot, plaza) and a quarter-turn rotation. It is a pure function of `CitySettings` — same
settings, same city — for the same reason `composeWorld` is (ADR-091): a world that is not
reproducible cannot be rendered offline.

Planning is deliberately separate from placing. The plan knows nothing about assets, so it can be
tested exhaustively without a library, a device or a mesh: that roads join, that no plot sits in a
carriageway, that a junction has roads on the sides it claims to. Placement then turns a plan plus a
library into clouds, and that is where the terrain, the asset scales and the art direction enter.

## One module size, in metres

Kenney's kit is modelled on a 1×1 unit tile with a 0.79-unit person; Quaternius' downtown reaches 28
units. Neither is metres. `CitySettings::moduleSize` is the lattice pitch in metres and every pack is
scaled to it through the manifest's `preferredScale`, exactly as `assets/city.manifest.json` already
reconciles a Kenney sedan against a Quaternius tower. **No geometry is rescaled on disk.** A pack
whose pieces do not tile at the chosen module is a pack that cannot be used as road, and that is a
curation decision rather than a reason to edit meshes.

## Consequences

**Good.** The instanced half costs what a scatter layer costs, which is already measured and already
optimised. The named half is addressable by every tool that exists — the editor, the sequencer's
actors and scene slots, the assistant's `scene.create_node`. Nothing new renders.

**Bad.** There are now two ways a city piece can exist, and "is this a node or an instance" is a
question an author can ask and get a surprising answer to. The layout must therefore record which
pieces it promoted to nodes and why, and a piece cannot be promoted after the fact without a rebuild.

**Watch for.** The temptation to promote pieces to nodes for convenience — to select one, to nudge
one — which would slide the whole city back to all-nodes one kerbstone at a time. Promotion is a
statement that something has a role in the film, not a workaround for the editor.
