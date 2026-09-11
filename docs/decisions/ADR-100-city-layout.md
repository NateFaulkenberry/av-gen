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

## A block's character governs three roles, not one

Buildings, the ground under them, and the props standing in their yards all come from the block's
one family. A suburban block is houses on grass with trees; an industrial one is sheds on paving with
tanks. The family is chosen once per block, from the seed and the block's coordinates, and every role
derives from that single name.

Roads, pavements, junctions and crossings deliberately do **not** split by family. A street is the
same street whichever block it runs past, and giving each family its own kerb is how a city stops
joining up — the exact failure `tileUnits` exists to prevent, arriving by a different route.

A family that declares no assets for a role falls back to the role's unfamilied list. So tagging a
family's ground is an improvement a manifest can make later, without every other family losing its
floor in the meantime.

## Props are not cells

A prop — a tree, a planter, a tank — is the first thing in this lattice that is not one piece per
cell. It is not what a cell *is*; it is what stands on it, and several stand on one. So:

- `propsPerCell` is a count, and the count varies per cell. A yard with exactly three things in it,
  on every yard, is a pattern rather than a yard.
- Props are jittered within `propSpread` of the cell centre, which is below half a module so a prop
  cannot cross into the cell beside it. For a tree beside a road, that crossing means standing in the
  carriageway.
- Props are turned **freely**, not by quarter turns. The rule that keeps road tiles square to the
  lattice exists because they have to meet their neighbours; a prop adjoins nothing. A row of trees
  all facing the same way is the clearest sign that a program placed them.
- Props take **pack scale**, like the ground, not the plot rule. A tree has no plot to fill, and
  stretching one to an 8 m footprint is how a garden ends up with a single enormous shrub.

Which pieces become props is curation, not a sweep. At this module a shipping container measures 24 m
long and a shopfront parasol reads as a 6 m umbrella standing on its own in a yard.

## A street steps

A block picks a family, which makes it uniform in character — and, on its own, random in height,
which no built street is. So a block also gets a height *band*: the family's pieces are sorted by
height over footprint (proportion, not raw height, because every building is scaled to the same
plot), the band names a place in that order, and each plot lands within one piece of it.

Neighbouring buildings then differ by a storey rather than by a tower, while two blocks of the same
family still differ from each other. The drift is deliberately small — the effect is lost entirely if
it is not.

## Headless has no UI to theme

Recorded here because it cost an afternoon and will again. `Application` constructs `imgui_` only
alongside the window. A call to `imgui_->applyTheme(...)` in the headless branch is therefore a
method call on a null `unique_ptr`, and it took down every `--headless` render, the `--render` batch
path and `tools/review_frames.py` with it — while the full test suite stayed green, because nothing
in it launches the binary headless. That is the coverage gap worth closing, not just the null.

## A piece tagged for the ground must have ground on it

A role tag says what a piece *dresses*. Nothing until now said it had to be *coverable ground*, and
the gap was not theoretical: `road-straight-barrier` — two rails with no carriageway between them —
was tagged `road`, drawn for half the road cells, and each one rendered as a hole through to the
background.

Every count said the city was complete, because it was: a piece was placed on every cell. `placeCity`
counts pieces; it cannot know whether a piece has a surface. A bounding box cannot tell either, since
the rails span the full tile and so the box is exactly a road's box. Only the triangles know, which
is why the test that now guards this asks whether any triangle lies under five points spread across
the tile — and not the corners, where a piece is allowed to stop short so a neighbour's kerb may
overhang into it.

The general rule: **a role tag is a claim about geometry, and claims about geometry get checked
against geometry.** Anything that becomes the floor under a cell is subject to it.

## What stands on a cell is not the cell

`CityPlacement::prop` separates the two. A tile is one module across and square to the lattice; a
lamp post and a tree are neither, and three tests were only distinguishing them by matching asset
names against a role list — which works until a piece is tagged for two things.

## Street furniture belongs to the road, so the road places it

Yard props are jittered across their cell because a yard has no preferred spot. Street furniture is
the opposite: it takes its position *from the carriageway beside it* — pushed out to the kerb, jogged
along it so a run of lamps is not a ruled line, and turned to face the traffic. A lamp in the middle
of a footway is in the way; one at the back is in a garden; a signal facing the pavement is pointless.

A pavement cell with no carriageway beside it is interior to a block and is furnished with nothing.

## The exception that makes the rule readable

A block is one family, which is what makes a street read as a street — and a whole city of such
blocks is a diagram. `cornerMix` lets a plot on a block's corner build from another family.

Corners specifically, because that is where two streets meet and where a shop stands. Scattering the
exception through the terrace would not read as variety; it would read as the family rule failing.

Which family a corner trades in comes from the seed, never from a name in code. `city_place.cpp`
does not know that "commercial" exists — it is a word an artist wrote in a manifest, and the rule is
"a family other than this block's".

The corner's *ground* follows its trade, since a shop standing on a lawn is the stranger result.
