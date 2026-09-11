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

## Buildings are not tiles

Ground pieces and buildings are scaled by different rules, and the difference is the whole reason
this section exists.

A **ground piece** takes one scale for the entire pack: `moduleSize / tileUnits`. It has to *meet*
its neighbour, so what matters is the tile it was drawn on, not its bounding box. `tileUnits` is
declared by whoever curates the pack and cannot be recovered from a mesh: `road-side` bounds
1.0 × 1.31 because its kerb is *meant* to overhang the tile, and scaling by that box shrinks the tile
it fills to 6.1 m of an 8 m cell, leaving a gap beside every one of them.

A **building** is scaled to its plot: `moduleSize * plotFill / max(footprint.x, footprint.z)`. It has
no tile to meet a neighbour across — a plot is bounded by pavement on all four sides — and its
footprint is its own. The same Kenney kit ranges from 0.9 to 1.3 units across, so the pack scale
would leave some buildings overhanging the footway and others adrift in the middle of their plot. The
scale is uniform, so a building keeps the proportions it was drawn with; a 3.15-aspect tower stays a
tower rather than being squashed into a cube.

`plotFill` is below 1 so buildings on adjacent plots do not touch.

### A pivot is not a bounding box

`naturalSize` is an *extent* and cannot say that a mesh hangs off to one side. Six of the buildings
in this pack are modelled about a corner: `industrial-building-h` spans −0.58 units to one side,
which at plot scale is 4.6 m — half the building in the road. `AssetDescriptor::naturalCentre`
records the bounds centre in the mesh's own units, and buildings are shifted by it (rotated with the
piece, so it still lands on the plot after the building has been turned to face its street).

Ground pieces are deliberately *not* recentred, even though `road-side` measures off-centre by the
same arithmetic: there, the offset is the kerb overhanging its tile on purpose, and recentring would
pull the kerb into the carriageway. Same measurement, opposite meaning — so the correction is applied
by what is being placed, never by the number alone.

## A block builds from one family

A building is chosen from its block's family, not from the whole library, and the family is a
function of the seed and the block's coordinates. Drawing each plot independently gives a bungalow
between two towers on every block — the characteristic look of a city nobody planned.

A family is declared by a `family:<name>` tag. The prefix is the point: the code must be able to tell
a family from any other word an artist writes, and every one of these buildings is *also* tagged
`city` and `building`. The rule "a tag some but not all of them carry" would work today and silently
reclassify the vocabulary the moment somebody tags one piece `damaged`.

## A block has a back

Buildings face the nearest carriageway. This is decided in `planCity` rather than in the placer,
because it needs only the lattice — so "every building has street frontage" is checkable with no
asset, mesh or device, which is the same argument that separates planning from placing above.

That check found a structural bug: a block 5 cells across has a plot in the middle with no frontage
in any direction, whose front door opens onto the back of the building in front of it. So a block's
core is built on its *perimeter*, and what is left becomes `CellKind::Courtyard` — the back of the
block, where the bins and the parking are. This is a perimeter block, which is what most real ones
are. It also gives the layout somewhere to put the things that belong behind a building rather than
on a street.

## A plot is ground plus a building

Every other cell is its own floor — a road tile *is* the road. A building is a solid thing standing
on something, so a plot emits two pieces: ground, then the building on it. A plot given only a
building has the void around its feet, at `plotFill` width, and it reads as a hole in the city.

The two draw from separately salted streams, so the floor's choice is its own. Sharing a stream would
make every plot that drew building two also draw ground two, and the pair would move together for no
reason a viewer could see.

## Stylized shading discards base colour textures

Not a decision of this ADR, but the thing most likely to waste an afternoon here. `pbr_shade.wgsl`
samples the base-colour texture's RGB only when `frame.lightCounts.z` is 0 — that is
`scene::Environment::stylized`. Under stylized shading a palette-textured import renders bone white,
correctly lit and completely colourless. Every Kenney kit is palette-textured, so a city scene sets
`"stylized": false`.
