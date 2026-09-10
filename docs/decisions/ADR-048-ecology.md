# ADR-048: Ecology

## Status
Accepted, 2026-09-09.

## Context
ADR-046 made geography and ADR-047 made places. What was still missing is the thing the world is
for: something growing on it. The requirement is not "scatter meshes on a heightfield" -- that is a
weekend's work and it looks like it. The requirement is that what grows somewhere and what the
ground looks like there are the *same decision*, because if the ground says marsh and the scatter
says forest, no amount of tuning makes them agree.

## Decision

### A scatter layer reads the biome weights
Not rules of its own. A layer says how densely it occurs in each biome by name, and the placement
pass multiplies those densities by the weights the ground colour is blended with. One source of
truth, consulted twice. Filters beyond that (`maxSlope`, `avoidWater`, `minAltitude`) are for things
true in *every* biome -- a fern does not grow on a cliff or under water wherever it is.

Naming a biome that does not exist is an error at load. A layer that was authored, parsed, built,
and grew no plants, with nothing anywhere saying why, is the worst outcome available.

### Placement is a jittered grid sized by the layer's own density
One cell per expected instance at the layer's peak density: a tree layer at 0.002 per square metre
walks a 22 m grid, grass at 0.4 walks a 1.6 m one. The cost is then proportional to the number of
instances rather than to the world's area times the number of layers, which is what makes eleven
layers over 400,000 square metres take a fraction of a second.

Everything is a pure function of the map, the layer and its seed. The same world always grows the
same forest.

### Clustering is world-space noise, not a per-layer accident
Plants occur in patches with gaps between them. The patch field is world space and independent of
the placement grid, so two layers that share a `clusterScale` clump in the *same* places -- which is
what makes a fern and a mushroom look like they are growing together rather than like two
independent scatters that happen to overlap.

### `DistributionKind::Scatter`: placements from outside
Every other distribution is a formula the object evaluates for itself. This one takes a supplied
point cloud, exactly as `PrimitiveKind::Mesh` takes a supplied mesh, and its structural hash is a
number the supplier provides. A scatter layer is then an ordinary procedural object: the instancing,
GPU culling, LOD, per-instance variation and material path are all the ones an imported mesh already
gets. Nothing new is drawable.

### A layer says how tall a thing should be, not what to multiply it by
Quaternius grass is 1.8 units tall and its trees are 7. A scale factor is a number about the file;
`height` is a number about the world. The normalisation is applied by whoever resolves the asset,
because only they know how big it is, and it goes on `sourceTransform` -- `distributionTransform`
would scale the placements along with the mesh and move a tree scaled x2 twice as far out.

### Textures from the library, palette from the world
A layer's `tint`, `emissiveColor` and `emissiveIntensity` multiply and add to the asset's own
material, using the split ADR-044 established. A library authored in daylight greens is otherwise a
library authored in daylight greens whatever the moon is doing. This is also where bioluminescence
enters: the fungi are emissive because the layer says so, not because the asset is.

## Consequences
Eleven layers over the shipped world place about 38,000 instances and cost a fraction of a second.
A 720p offline frame went from 14 ms of bare terrain to 25 ms with the ecology on it.

Filters sample the surface at a fixed half metre rather than at the layer's cell size. Deriving the
epsilon from the cell would measure a sparse layer's slope over eleven metres and a dense one's over
one, so the same point would be steep for one layer and flat for another -- an accident of density
masquerading as a decision.

Densities hash as a set. They are written as a JSON object, whose key order is not preserved, so
hashing them in the order they arrive makes a file that round-trips report itself as changed.

Not done, and each is a phase of its own: nothing is placed *relative to* anything else (no
undergrowth in a tree's shadow, no moss on the boulders it is next to); there is no wind; and the
emission is constant rather than a living field. (Multi-material assets were the fourth item here
and are done: a layer whose asset carries several materials now emits one object per material, all
from the same cloud -- ADR-044's 2026-09-10 addendum.)
