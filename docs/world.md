# The world

A world is the authored description of a place: geography first, then what grows on it. The
hierarchy is deliberate and one-directional --

```
WORLD -> TERRAIN -> BIOMES -> ECOLOGY -> BIOLUMINESCENCE -> ATMOSPHERE -> CINEMATOGRAPHY
```

-- because each stage is a function of the one above it. Vegetation belongs on a slope, not
scattered on a plane and then given a slope later.

This document covers the first two stages (ADR-046). The rest are not built yet.

## The world map

`world::WorldMap` is data, sampled as a pure function of position:

```cpp
float h  = map.height({x, z});
world::Sample s = map.sample({x, z}, epsilon);  // height, normal, slope, water surface
```

Nothing in `src/world/world_map.*` knows about meshes, chunks, the GPU or the camera. That is what
lets a terrain builder, a scatter pass, a biome lookup and a command-line preview tool all agree
about where the ground is without sharing anything but the map.

### Noise makes terrain; features make geography

An fBm field gives you terrain. It does not give you a *place*: somewhere reads as a place when it
has things you could name and navigate by. So a map is a small stack of noise octaves plus an
ordered list of `Feature` stamps, each a polyline in world space whose points carry their own level:

| kind | what it does |
|---|---|
| `ridge` | raises by `amplitude` along its line |
| `valley` | lowers by `amplitude` |
| `river` | cuts down to `level - amplitude`, and holds water at `level` |
| `flat` | blends toward `level`: a plateau, a lake bed, a clearing, a terrace |

A path with one point is a radial feature (a mound, a crater, a clearing); two or more is a
corridor. Every feature also carries `roughness`, which damps the base noise inside it -- that is
how a river bed comes out smooth and a clearing comes out level without carving a smooth shape out
of rough ground and leaving a rim around it.

```json
{ "name": "glowmere-run", "kind": "river",
  "path": [[-24, 22, -246], [2, 12, -172], [14, -3.4, -38], [-38, -17, 248]],
  "width": 7.0, "amplitude": 2.4, "falloff": 1.4, "roughness": 0.12,
  "smoothing": 3, "water": true }
```

`smoothing` applies Chaikin corner cutting before the path is sampled, because a polyline river has
visible straight reaches and mitred bends. Chaikin rather than a spline because its curve stays
inside the convex hull of the control points, so a smoothed river can never climb above a level it
was authored to descend through.

### Sampling order

1. feature weights, from distance to each path
2. base noise, damped by the smallest `roughness` any feature asks for
3. raise and lower
4. flatten toward the interpolated path level
5. water features cut down to their bed

Step 5 is separate from step 4 for a reason worth stating: an additive carve subtracts a fixed
amount, so a river crossing high ground keeps a bed above its own water surface and renders as a
chain of disconnected puddles. Water has to cut *down to* a level.

### Other knobs

- `erosion` (0..1) turns the octave sum into a multifractal: detail collects on crests and drains
  out of hollows. One multiply per octave, no extra noise. It is not an erosion simulation, and at
  the shipped 0.35 it is a mild redistribution rather than a transformation.
- `heightImage` blends a painted greyscale PNG over the whole extent, for worlds an artist would
  rather draw than describe.
- `seed` reseeds every octave. Features do not move.

## Terrain

`NodeKind::Terrain` turns a map into ground:

```json
{ "name": "valley", "kind": "terrain",
  "world": { "seed": 20260909, "size": [640, 640] },
  "terrain": { "chunkSize": 40.0, "resolution": 32, "lodLevels": 4,
               "lodDistance": 70.0, "viewDistance": 520.0, "skirtDepth": 5.0 },
  "material": { "baseColor": [0.023, 0.144, 0.087], "roughness": 0.9, "program": "valleyGround" } }
```

Omit `world` entirely and you get the shipped one. Every chunk's meshes, at every level, are built
once and uploaded once; per frame the only thing that changes is which mesh each chunk entity points
at and whether it is visible. Terrain therefore costs nothing to fly through.

Debug parameters, per node: `terrainLod`, `terrainCull`, `terrainLodDistance`, `terrainViewDistance`.
Turn LOD off to find out whether a shading artefact is a level boundary; turn culling off to see
what culling was removing.

### Terrain materials read the biome axis and the slope from uv

Terrain has no unwrap worth having, and material programs reach world space directly through
`Triplanar` and `WorldProject`, so uv carries the two scalars a terrain material actually needs:
`uv.x` is the biome axis and `uv.y` is the slope. Neither is reachable otherwise, because mask ops
read a register's x channel.

Getting a value *into* that channel is what `swizzle` is for -- it is the primitive every shading
language has and this op set did not:

```json
{"kind": "input",   "dst": 0, "input": "uv"},
{"kind": "swizzle", "dst": 1, "srcA": 0, "constant": [0, 0, 0, 0]},
{"kind": "swizzle", "dst": 2, "srcA": 0, "constant": [1, 1, 1, 1]}
```

r1 now holds the biome axis in every channel and r2 the slope, and both can drive a mask.

## Biomes

A biome (ADR-047) is a set of soft-edged bands over what the map already knows -- altitude, slope
and moisture -- plus places it simply is:

```json
{ "name": "marsh",
  "altitude": [0.0, 0.30, 0.20], "slope": [0.0, 0.16, 0.14], "moisture": [0.70, 1.0, 0.22],
  "weight": 1.4,
  "groundColor": [0.0044, 0.1046, 0.0844], "rockColor": [0.0132, 0.0742, 0.0700], "roughness": 0.82,
  "regions": [{ "path": [[4, -14]], "width": 46, "falloff": 1.4, "strength": 2.2 }] }
```

A range is `[lo, hi]` or `[lo, hi, fade]`, where `fade` is how far outside the band the membership
takes to reach zero. Every biome scores a point, the scores are normalised, and what comes back is
a blend -- so a transition is a band, and everything that reads the weights crosses over together.
The score is a *product*: a biome must satisfy all three of its ranges to be anywhere at all.

Moisture is defined once, by the map: a falloff from the nearest water feature's bank over
`moistureReach` metres, or `lowlandMoisture * (1 - altitude)`, whichever is larger.

### The set is ordered, and the order matters

A material program masks on a register's x channel and a terrain vertex has two floats, so the blend
is collapsed to one scalar -- its position along the authored order -- and carried in `uv.x`, with
the shading slope in `uv.y`. That is what lets a palette be blended along it.

The constraint that buys: **neighbours in the list must be neighbours on the ground.** Two biomes
far apart in the list that meet will blend through the colours in between. Keep a set small and
ordered like a gradient, and make the palette a *path* through hue rather than a spread around it.
An olive meadow between a green forest and a violet scree made every transition rainbow.

### Write the bands against the world, not against round numbers

`avgen_world_preview --biomes` prints the slope and moisture percentiles and the share each biome
ends up owning. The first scree band asked for slope above 0.34, which sounds steep; this terrain's
slope has a median of 0.065 and a 99th percentile of 0.31, so scree owned one per cent of the map.
The shipped set is 15% marsh, 18% meadow, 37% forest, 22% scree, 9% rim, and a test holds every
biome between 4% and 60%.

### The ground material is generated

Unless the scene names a program, the terrain node's material is built from the biome set: two
three-stop ramps along the axis crossed in the middle, a cliff mask on the slope, world-space
mottling. So a biome's colours are authored in one place -- the world -- and the shader that paints
them follows. Retune a `groundColor` and the ground changes with no shader editing.

Biome rules read a slope measured over about eight metres, not over one vertex. A rule fed
mesh-scale slope puts a boundary on every ripple, and a hundred one-vertex boundaries across a ridge
is a sawtooth rather than an ecotone.

## Ecology

A scatter layer (ADR-048) names an asset and says how densely it occurs in each biome:

```json
{ "name": "fungi", "asset": "../../assets/quaternius/glTF/Mushroom_Common.gltf",
  "densities": { "marsh": 0.030, "forest": 0.008 },
  "height": 0.28, "minScale": 0.6, "maxScale": 2.2,
  "maxSlope": 0.22, "alignToGround": 0.35, "avoidWater": true,
  "clusterScale": 9.0, "clustering": 0.85,
  "tint": [0.52, 0.35, 0.82], "emissiveColor": [0.25, 0.06, 0.72], "emissiveIntensity": 5.5,
  "seed": 38, "meshBudget": 240, "maxInstances": 6000 }
```

Densities are per square metre where that biome is at full weight, multiplied by the biome weights
the ground colour is blended with -- so what grows somewhere and what the ground looks like there
are the same decision. Naming a biome that does not exist is an error at load, not an empty forest.

Three things worth knowing before authoring one:

- **`height` is metres, not a multiplier.** Quaternius grass is 1.8 units tall and its trees are 7,
  so a scale factor is a number about the file. The first pass put two-metre grass under
  seven-metre trees.
- **`clustering` uses a world-space field**, so two layers sharing a `clusterScale` clump in the
  *same* places. That is what makes a fern and a mushroom look like they are growing together.
- **`tint` and `emissiveColor` are how a library becomes this world.** The asset's textures stay;
  these multiply and add. Bioluminescence is a layer saying its fungi are emissive.

Each layer becomes one ordinary procedural object with `distribution.kind = "scatter"` -- placements
supplied from outside, the way an imported mesh is a source supplied from outside. Everything
downstream is the instancing, culling, LOD and material path that already existed.

## Looking at a world before it has triangles

```
./build/debug/tools/avgen_world_preview [world.json] [out.png] [pixels] [x z ...]
```

Renders any world as a hypsometric map with hillshade, 10 m contours and water, and prints the
height, slope and wet/dry state at each probe point. With no arguments it previews the shipped
world. This is how a river gets moved and a camera gets placed: both are cheaper to judge on a
topographic map than on a render.

## The shipped world

`glowmere`: a bowl 640 m across with 125 m of relief, walled to the north by a rim and to the east
and west by lower arms, a valley running north to south down the middle, a meandering river in it,
a tarn off the west bank, two shelves on the west slope and a clearing near the origin. The camera
starts in the south looking north, so what it sees is foreground hollow, midground river, background
rim -- three depth planes that exist in the geometry rather than being faked by fog.

Every number in `defaultWorld()` is a decision rather than a default. It is shipped rather than left
to each scene because "make a landscape" is a design job, and an empty noise field is the flat plane
by another name.

## Costs

640 m at 40 m chunks and 1.25 m spacing: 256 chunks, 590k triangles at LOD 0, ~18 MB of vertex data,
**28 ms** to build at load in a release build (1.3 s in a debug one). It was 2.5 s and 30 s, and the
89x came from two changes and one measurement:

- A chunk samples its heights **once**, on a grid at LOD 0 spacing with a one-cell border, and every
  level of that chunk is a stride through it. That is one height evaluation per point instead of the
  five an analytic normal needs, and it is shared across four levels instead of repeated. The border
  cell is what lets an edge vertex use a centred difference, so two chunks still agree bitwise along
  a shared seam.
- Chunks are built **across the machine's cores**. Each thread owns whole chunks and writes only its
  own slots; the shared state is the map, and sampling a map is a pure function. `emit` is still
  serial, because it hands meshes to the Scene.
- The cost was then dominated by asking every feature how far away every sample was -- a smoothed
  river is a hundred segments, and most samples are nowhere near it. Each feature now carries the
  box outside which its weight is exactly zero, which is one compare and cuts the debug build by 3x.

At 2880x1800 a frame of bare terrain is 11--14 ms of GPU time, which is the same as the fully
dressed Kenney grove and about the same as the grove takes windowed. Terrain is not what the frame
is spent on: with volumetrics off the same frame is 12 ms, so the volumetric march remains the dial,
as it was before there was a world.

`Scene::bounds()` is called every frame to size the shadow cascades, and it used to rescan every
vertex of every visible mesh to do it. Terrain made that a few hundred thousand vertex reads per
frame; mesh bounds are now cached against `meshVersion`.

## The sky (ADR-049)

Glowmere Valley's sky is Kloppenheim 02 Pure Sky at 4K, a photographed moonlit night. The moon in
frame and the moonlight on the terrain are the same moon: `lightFromEnvironment` aims the key light
away from the map's brightest direction, and `rotation` turns both together, so composing the shot
cannot desynchronise them. The sky is drawn at `skyIntensity` 0.08 and lights at `intensity` 0.3 --
dark enough to read as night, bright enough to keep the shadowed flanks off black.

`examples/world/moonrise.scene.json` is the same world under a different sky, changed by editing
JSON alone: a different `map`, `rotation` and two intensities.

One thing the sky changed that was not about the sky. The valley's volumetrics ran a
Henyey-Greenstein `g` of 0.6, and a moon in frame is a moon near the view axis, so the mist lit up
and swamped the ecology. `g` is now 0.12 with less in-scatter. The old value had only ever been
seen with the key light off to the side.

## Known limitations

- Frustum culling sets `Entity::visible`, which the shadow pass also honours, so a chunk behind the
  camera stops casting into the frame. Not visible with the low keys this world uses; the fix when
  it matters is to cull against a frustum extended along the light direction, not to stop culling.
- LOD is chosen by distance, not by screen-space error, so a `lodDistance` tuned for one focal
  length is wrong for another.
- There is no water surface yet. A river reads as a dark notch until phase 4.
- Nothing is placed relative to anything else: no undergrowth in a tree's shadow, no moss on the
  boulder it is beside.
- Quaternius meshes carry several materials each and `mergedAssetMesh` collapses an asset to one,
  so a mushroom's cap and stem share a colour.
- Emission is constant. Making it a living field is what the chromatic phase is for.
- The sky's stars are the photograph's. A camera that looks mostly at the ground, as this one does,
  sees only the few degrees above the ridge, which on this HDRI is where the moon's haze is
  brightest -- so the frame gets a moon and a horizon glow rather than a field of stars.
