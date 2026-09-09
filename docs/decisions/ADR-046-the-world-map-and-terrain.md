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
of vertex data, and roughly 2.5 s to build at load. The build is single-threaded and obvious to
parallelise if it becomes a problem; it has not yet.

Frustum culling sets `Entity::visible`, which the shadow pass also honours, so a chunk behind the
camera stops casting into the frame. That is wrong in principle and has not been visible in
practice with the low keys this world uses. The fix, when it is needed, is to cull against a
frustum extended along the light direction rather than to stop culling.

Terrain exposed a renderer bug that no previous scene could: GTAO counted samples coplanar with the
surface as occluders, so any smooth ground seen at a grazing angle lost roughly half its ambient
light to a false grey wash. Objects viewed from a normal angle never showed it. A height test
against the tangent plane fixes it, and `tests/rendering/test_shadows_gpu.cpp` now asserts that a
plane keeps its ambient -- an assertion the previous AO tests, which only checked a crease and
determinism, had no reason to make.
