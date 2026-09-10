# ADR-046: The world map and terrain

## Status
Accepted, 2026-09-09.

## Context
Every scene in this engine so far has stood on a flat plane. `examples/world/grove.scene.json`
made that literal: its ground was a 400 x 400 metre box, 0.6 m thick, and everything interesting
was scattered on top of it. That is fine for a shot composed at one focal length from one position,
and it is the reason none of those shots could move. A camera that travels needs somewhere to
travel through, and depth that comes from geometry rather than from fog.

The requirement is a world: real geography that terrain, biomes, ecology, water and atmosphere are
all derived from, in that order. This ADR covers the first two of those.

## Decision

### The world map is data, sampled as a pure function
`world::WorldMap` is the single authored description of a place. `height(x, z)` is a pure function
of position and a seed; `sample()` adds the normal, the slope and the water surface. Nothing in the
module knows about meshes, chunks, the GPU or the camera, which is what lets terrain, a scatter
pass, a biome lookup and a debug tool all agree about where the ground is without sharing a cache.

### Noise makes terrain; features make geography
An fBm field produces terrain and does not produce a *place*. A landscape reads as somewhere when
it has things you could name and navigate by: this ridge, that valley, the river between them, the
clearing where the light lands. So a world map is a noise base plus an ordered list of authored
`Feature` stamps -- `Ridge`, `Valley`, `River`, `Flat` -- each drawn as a polyline in world space
whose points carry their own level in y. Moving a river is moving three numbers.

Evaluation order at a point, and why each stage is where it is:

1. feature weights, from distance to each feature's path
2. base noise, damped by the smallest `roughness` any feature asks for -- which is how a river bed
   comes out smooth and a clearing comes out level *without* carving a smooth shape out of rough
   ground and leaving a rim around it
3. raise and lower, additively
4. flatten toward the interpolated path level, so a river descends along its course
5. water features cut down to their bed

### Water cuts to a level; it does not subtract an amount
The fifth stage exists because the fourth cannot promise what water needs. An additive carve takes
a fixed depth out of whatever is there: take 5 m out of a 30 m hillside and the river bed is still
25 m in the air. The first implementation did exactly that, and the river rendered as a chain of
disconnected puddles wherever the surrounding ground happened to be low enough. A water feature
records the bed it wants and pulls the terrain down to it, weighted by its falloff so the banks
still blend out. A channel is then continuous along its whole length by construction.

### Corridors are smoothed, not straight
A polyline river has visible straight reaches and mitred bends, which is the loudest tell that a
watercourse was generated. `Feature::smoothing` applies Chaikin corner cutting to the path before it
is sampled. Chaikin rather than a spline because its curve stays inside the convex hull of the
control polygon: a smoothed river can never rise above a level it was authored to descend through.
The authored `path` is what round-trips through JSON; the smoothed `curve` is runtime only.

### Erosion is a knob, not a simulation
`erosion` turns the octave sum into a multifractal: each octave's amplitude is scaled by how high
the coarser octaves already put this point, so detail collects on crests and drains out of hollows.
It is one multiply per octave and no extra noise evaluations. It is not an erosion simulation and
does not claim to be; at the default 0.35 it is a mild redistribution, and the honest description of
what it buys at these amplitudes is "some", not "transformative".

### An artist can repaint the world without touching C++
A world is a JSON object -- seed, extent, octaves, features -- inside a scene file or on its own.
`heightImage` optionally blends a painted greyscale PNG over the whole extent. `avgen_world_preview`
renders any world to a hypsometric map with hillshade, contours, water and height probes, so
geography can be judged and camera positions found before a triangle exists.

### Terrain is a node kind, and rides the entity path
`NodeKind::Terrain` flattens into one entity per chunk. Every chunk's meshes, at every LOD level,
are built once and uploaded once; per frame the only thing that changes is which mesh each chunk
entity points at and whether it is visible. That means terrain costs nothing to fly through, and it
needed no renderer changes at all: entity visibility and mesh selection are machinery the scene
already had.

LOD cracks are hidden by a skirt -- a short vertical curtain around each chunk, lit with the border
vertex's own normal so where it shows at all it reads as a continuation of the ground. Normals are
sampled at the LOD 0 spacing whatever level is being built, so two chunks meeting at different
resolutions shade continuously even though their silhouettes differ.

### Terrain uv carries slope and altitude
Terrain has no unwrap worth having, and the material op set (ADR-030/036) reaches world space
directly through `Triplanar` and `WorldProject`, so a tiling uv would be the one thing on the vertex
that nothing reads. What a terrain material does need is how steep this point is and how high --
and neither is reachable from a material program otherwise, because the mask ops read a register's
x channel and both of those live in the y channel of the inputs that carry them. So uv is
`(slope, altitude)`, altitude normalised over a survey of the whole map so every chunk agrees on
where "high" is.

## Consequences
A 640 m world at 40 m chunks and 1.25 m spacing is 256 chunks, 590k triangles at LOD 0, about 18 MB
of vertex data, and 28 ms to build at load (1.3 s in a debug build). The first version took 2.5 s
and 30 s respectively, which was slow enough to read as a hang; `docs/world.md` records the three
changes that closed it. The one worth repeating here is that a chunk samples its height field once,
at LOD 0 spacing with a one-cell border, and every level of that chunk is a stride through it --
that is both the optimisation and the reason the levels agree exactly.

Frustum culling used to set `Entity::visible`, which the shadow pass also honours, so a chunk
behind the camera stopped casting into the frame. Fixed 2026-09-10, and not the way this ADR
predicted: no extended frustum was needed, because the shadow pass already culls each caster
against the cascade's own frustum -- which *is* the light-direction test. The bug was suppressing
the candidate before that test ever ran. `Entity::cameraCulled` is the camera's verdict and nothing
else's: the camera passes skip it, the shadow passes still receive it, and the entity loop gives a
camera-culled caster a uniform slot only when some cascade can see it, after every on-screen entity
has taken one. Distance is a separate claim and still removes a chunk outright -- nothing beyond the
view distance can reach a cascade, which only ever covers the near part of the camera's frustum.

Terrain LOD was a screen-space decision fed a *reference* viewport height of 900 px rather than the
real one, so a scene picked the same levels whatever it was being rendered into: the same ground
came back at the same mesh in a thumbnail and on a 5K display, which is exactly backwards. That was
a bug, not a stability choice -- the point of a screen-space metric is that it answers the question
about the image. Fixed 2026-09-10: `Composition::setViewport` is called once per frame by whoever
owns the render target, and the chunk cull frustum takes its aspect from the same place, floored at
the conservative 2.5 it always used so a wider viewport widens the frustum and a narrower one cannot
narrow it. The default is still 1440x900, so a caller that never sets it -- a test, a tool -- picks
exactly the levels it always did, and every `lodDistance` in every scene file still means what it
meant.

Terrain exposed a renderer bug that no previous scene could: GTAO counted samples coplanar with the
surface as occluders, so any smooth ground seen at a grazing angle lost roughly half its ambient
light to a false grey wash. Objects viewed from a normal angle never showed it. A height test
against the tangent plane fixes it, and `tests/rendering/test_shadows_gpu.cpp` now asserts that a
plane keeps its ambient -- an assertion the previous AO tests, which only checked a crease and
determinism, had no reason to make.

## Addendum, 2026-09-09: water

A water surface is built from the same chunk height field the ground is, at the same resolution, and
for the same reason: a river descends, so no single plane can be its surface. Vertex uv carries
(depth, shore) rather than a texture coordinate, and the material -- generated from `WaterSettings`
like the ground's is from the biome set -- colours by depth and fades opacity at the edge. A quad is
emitted wherever a corner is under water, and the terrain occludes the rest by depth test, so the
shoreline is where the two surfaces actually cross.

Getting there took four wrong guesses, which is worth recording because three of them were
plausible and one was checkable in a second.

The artefact was a flat slab of water standing several metres over the floodplain with a row of
vertical fins under it. I blamed, in order: the shoreline quantisation, the dry-corner fallback, the
terrain skirts, and the world's authored river level. The third was disproved by a hash of two
renders -- the change had reached the screen and the artefact had not moved -- and the fourth was
real but only a contributing cause.

The actual cause was that `waterSurface` returns `seaLevel` where there is no water, and `seaLevel`
defaults to -1000 as a sentinel for "this world has no sea". That is a *finite* number, so the mesh
builder's test for a missing surface (`!isfinite`) never fired, and every dry corner of a wet quad
was placed a kilometre underground. The fins were those quads. A test asserting that no water quad
contains a large vertical step finds it in one run; the first version of that test measured depth at
a vertex instead and failed the world for having a river in it.

Two authoring facts came out of it and are now written where the world is authored: a body's level
must sit below the ground beside it, by enough to cover how far the floor wanders around the line it
was flattened toward -- five metres here, not the two the paths nominally differed by. And a river's
`width` is its channel, not its floodplain.
