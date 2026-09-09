# ADR-047: Biomes

## Status
Accepted, 2026-09-09.

## Context
ADR-046 gave the world geography and turned it into ground. The ground was one material: a
600-metre valley in a single colour, which reads as a single object however good its silhouette is.
The next thing the world needs is to be *made of places* -- and the hierarchy says those places
must be derived from the geography rather than painted over it, because everything after this
(ecology, bioluminescence) has to agree with them.

## Decision

### A biome is a set of ranges over what the map already knows
Altitude, slope and moisture, each a band with soft edges, plus a weight. Moisture is the only one
that is not a property of a single point, so `WorldMap::moisture` defines it once -- a falloff from
the nearest water feature's bank, or a lowland term, whichever is larger -- rather than letting
every consumer invent its own.

### Weights, not an index
Every biome scores a point, the scores are normalised, and callers get a blend. A transition is
then a band rather than a line, and the ground material and (later) the scatter densities cross
over together because they read the same numbers. A point that satisfies nothing gets an even blend
rather than a hole, which keeps the result continuous in the gaps.

### The score is a product
A biome must satisfy every one of its ranges to be anywhere. Summed instead, alpine scree turns up
on the valley floor on the strength of its slope term alone.

### Regions
A biome may also name places it simply *is*, as polylines with a width. That is how the hollow the
opening shot is composed in becomes a grove because the shot needs one, rather than because the
moisture happened to land there.

### One ordered axis, carried on the vertex
A material program masks on a register's x channel and a vertex has two floats. So the blend is
collapsed to one scalar -- its position along the authored order of the set -- and carried in
`uv.x`, with the shading slope in `uv.y`. A material blends a palette along it and gets a band at
every transition for free.

The cost is an authoring constraint, and it is a real one: **neighbours in the list must be
neighbours on the ground**, because two biomes far apart in the list that meet will blend through
the colours in between. A set should read as a gradient -- marsh, meadow, forest, scree, rim -- and
should stay small. This is why the palette is a path through hue rather than a spread around it: an
olive meadow between a green forest and a violet scree made every transition rainbow.

Altitude left the vertex to make room, which is no loss: altitude is one of the three things a rule
is written in, so an altitude-banded look is now an authored biome instead of an implicit one.

### Biomes read the hillside's slope, not the vertex's
A rule fed per-vertex slope puts a biome boundary on every ripple, and a hundred one-vertex
boundaries across a ridge is a sawtooth. Biome evaluation uses a central difference over about
eight metres of the chunk's own height field -- free, since the field is already there -- while the
shading normal stays at mesh scale.

### The ground material is generated from the set
`terrainMaterialProgram(biomes)` builds the program: two three-stop ramps along the axis crossed
over in the middle, a cliff mask on the slope, world-space mottling. A scene that also wrote those
colours into a program by hand would have two copies of the palette to keep in step, which is how a
scene ends up with the ground painted in last week's colours. An artist retunes a biome in the world
JSON and the ground follows. A scene that wants something else names its own program.

## Consequences
The op that made this possible is new: `MaterialOpKind::Swizzle`, which moves a component into the
x channel every mask op reads. Without it a program can carry exactly one maskable scalar, which is
one fewer than terrain needs. It is general -- it is the primitive every shading language has and
this op set did not -- and its CPU and GPU implementations are asserted equal.

Ranges have to be written against the world's actual spread, not against round numbers. The first
scree band asked for slope above 0.34 on terrain whose slope has a median of 0.065 and a 99th
percentile of 0.31, and duly owned one per cent of the map. `avgen_world_preview --biomes` now
prints those percentiles and the share each biome ends up owning, and a test asserts no biome falls
below 4% or rises above 60% of the shipped world -- both of which happened while the bands were
being written.

The generated program was invisible on its first run: it was created inside the node loop, after
the pass that copies material programs into the Scene, so the terrain named a program that would
not exist until the next rebuild -- and a scene that rebuilds once never has one. Generation now
happens before that copy. The lesson is the same one this project keeps relearning: the change that
looked like it worked and the change that actually reached the screen are different claims, and
only a render distinguishes them.

Not done: biomes do not yet influence anything but the ground colour. Scatter densities, water tint
and the chromatic field are all meant to read the same weights, and each is a later phase.
