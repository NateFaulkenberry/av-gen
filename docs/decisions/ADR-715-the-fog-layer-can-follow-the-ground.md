# ADR-715: the fog layer can follow the ground

Status: accepted. Date: 2026-09-24. Implements ADR-575 §18 (the owner's ruling: DO IT), per
`docs/design/effect-library/tornado-fog-production-pass.md`, Ruling 1.

## Context

ADR-575 §18 measured five routes to "fog that hugs the world". Two already worked (scene depth and
world height). The terrain heightmap had no GPU resource: the ground's height lived only in
`WorldMap::height` and `TerrainQuery::heightAt`, which are CPU calls. §18 recommended baking a
terrain height texture once and binding it to the volume pass, plus a `fogGroundFollow` control. The
owner ruled to do it.

ADR-567 and ADR-568 set the constraint. One height layer has **three** readers: the march
(`volume.wgsl`), the surface fog's closed-form integral (`common.wgsl`) and the particle transmittance
estimate (`particles.wgsl`). A test checks that the first two agree. A layer whose top follows the
terrain has no closed-form integral along a ray over an arbitrary terrain, so the surface pass needed
a rule as well as the march.

## Decision

### The bake

- **Where it is made.** `world::bakeTerrainHeight` samples `WorldMap::height` on the same cache miss
  that builds the chunk meshes. The result is stored as `TerrainProducts::height`, under the same key
  as the meshes. A rebuild that leaves the terrain alone reuses it by pointer, and a test checks that.
  Glowmere's 640 m map bakes in about 21 ms on the thread pool. Nothing is done per frame.
- **Grid.** The grid is vertex-aligned: sample (i, j) is at `origin + (i, j) * spacing`, and the first
  and last samples sit on the map's edges. Every grid sample equals `WorldMap::height` bit for bit.
- **Resolution.** Samples are **2 m** apart, and the spacing only grows when a side would pass 1024
  samples. For Glowmere that is 321 × 321, about 400 KB. The terrain mesh itself is 1.25 m at LOD 0.
  A fog layer's features are metres to tens of metres tall, and bilinear filtering keeps a valley floor
  continuous at 2 m. A test checks that a V-shaped valley comes back exactly at 2 m.
- **Extent.** The bake covers the map's footprint. `world::TerrainGround` places it under the terrain
  node's translation and per-axis scale. A rotated terrain is not baked; the builder logs a warning and
  that terrain reads as flat.
- **Format.** R32F. The shader reads it with `textureLoad` and does the bilinear by hand, because R32F
  cannot be filtered without an optional device feature. The four-load bilinear is identical on every
  GPU, and `TerrainGround::groundAt` is its CPU twin.
- **Outside the footprint.** The edge height fades to 0 across 5% of the footprint (32 m on Glowmere).
  Beyond that band the layer is the flat plane.
- **More than one terrain.** Only the first terrain in a scene is baked; the builder warns about any
  others.

### Binding

- **Frame group.** The bake is bound at group 0, binding 12, with its placement in
  `FrameUniforms::terrainMap0/1`, appended at the end of the struct. The march reads it through the
  frame group, the same way the surface fog does, so the two readers cannot be given different
  terrains. `volume.wgsl` gains only the ground branch in `volumeDensityAt` and a comment on the
  `heightFog` lane.
- **Particles.** The particle render group gets the same texture at its own binding 12, plus
  `terrain0/1` lanes. ParticleUniforms grows from 39 to 41 vec4s.
- **No terrain.** A 1×1 placeholder is bound and never read.
- **Why not the `volumeDensityField` extension point.** ADR-568 suggested it. A scalar field multiplies
  the density; it cannot move the layer's reference height, and the surface pass cannot reach it.

### `fogGroundFollow` (0..1, default 0)

- **Semantics.** The layer's top is `fogHeight + follow * ground(x, z)`. At 0 the layer is the flat
  plane. At 1, `fogHeight` is measured from the terrain directly below the sample.
- **Where it lives.** `scene/fogGroundFollow` is an ordinary parameter in the environment's fog group,
  hard-clamped to 0..1, so it can be modulated. Both serialisers keep it:
  - the scene file writes the key only when it is not 0, so older files round-trip to the same bytes;
  - the project document carries it through `parameters`.
- **UI.** The Environment panel draws it as "Follow ground", after "Height curve". When the scene has
  no terrain, the row says so.
- **0 is bit-identical to before.** Each reader keeps its old expression on the flat branch. The ground
  branch runs only when `follow > 0` **and** a terrain is bound (`terrainMap1.w`). The CPU also zeroes
  the follow when there is no terrain.
- **The march** evaluates `fogGroundProfileAt` exactly, once per sample.
- **The surface pass** (`fogGroundMean`) splits the ray into 8 pieces and treats the ground as linear
  within each. Each piece is then the closed-form difference quotient from ADR-058.
  - On ground that is affine along the ray this matches the march exactly.
  - On a V-shaped valley, the worst gap is 9.1% of the air at 4 pieces, 2.6% at 8 and 0.5% at 16.
    8 pieces cost 9 ground reads, and only on the follow branch.
- **The particle estimate** uses the march's term at each of its four samples.

## Evidence

The renders are in the session scratchpad (`terrainfog/`), at 960×540, t = 2 s.

### What the renders show

- **Glowmere as shipped** (surface fog only: layer top 4 m, falloff 0.1, density 0.019).
  - Establishing shot: 65% of pixels move by more than 2 levels at 0.5, and 85% at 1.
  - Aerial: 32% at 0.5 and 70% at 1.
  - At 1 the far hillsides and the ridge haze over; the change is subtle by eye.
- **With the march added** (0.006): nearly identical to the surface-only arm. The march is faint at
  that density.
- **Fog lab on the same terrain** (dense layer, compact top at 3 m, both readers): 7% of pixels move at
  0.5 and 69% at 1. At 1 the fog climbs the right-hand slope and the far floor; at 0 it sits in the
  river valley.

### The honest reading: follow reduces pooling, it does not create it

The ruling describes this control as "fog sits in valleys". The semantics it specified do something
else:

- At follow 0, the flat plane already fills the valley floor wherever the floor lies below
  `fogHeight`. In Glowmere, the valley floor *is* the low ground.
- The fog's depth at a point is `fogHeight - (1 - follow) * ground`. Raising the follow therefore
  **reduces** the contrast between valley and ridge.
- At 1 the fog is a blanket of constant thickness that climbs the hills.

What the control actually buys is fog that sits in a valley **wherever that valley is**: a high basin
above the plane, or a terrain that is not near altitude 0. It does not make a valley foggier than the
flat plane already does.

### Proposed follow-up (not built)

To make fog pool in every valley, measure the layer from a **low-passed** ground: the baked height
blurred over roughly 50–100 m.

- In a valley the blurred height is above the real ground, so the layer is deeper there.
- On a ridge the blurred height is below the real ground, so the layer is thinner there.

This would be a second bake from the same heights plus one more control. The shader rules above are
unchanged: the chord rule integrates any ground the bake provides.

## Tests

**CPU** (`test_terrain_height.cpp`):
- A flat map and a V-shaped valley give known heights. Grid samples equal `WorldMap::height` bit for
  bit, and values between samples follow the formula.
- The placement is correct, including the fade outside the footprint. A rotated terrain or an empty
  bake gives no ground.
- The spacing is 2 m until a side would pass 1024 samples.
- A terrain node bakes once and the scene carries the bake. A rebuild for an unrelated reason reuses
  the same object.
- A scene with no terrain has no ground to follow.
- The control reaches the environment through the modulator, and `toJson` round-trips it.
- The value survives the project document after two frames have run. The control arm: a fresh load
  without the project reads 0.

**GPU:**
- `test_height_fog_gpu.cpp`:
  - The surface pass and the march agree on a sloped ground to 0.02 m of air, over 216 cases.
  - The ground matches the CPU twin.
  - The profile is held against a CPU restatement of the altitude.
  - On a valley the gap is measured and bounded under 5%.
- `test_terrain_fog_gpu.cpp`:
  - The shipped `applyFog` at follow 0, and with no terrain at any follow, is bit-identical to the
    pre-ADR-715 function restated verbatim. At 0.5 it differs, which is the control.
  - A render at 0 is byte-identical with the terrain removed, and also with a different terrain bound.
  - Without a terrain, follow does nothing.
  - With a terrain, each reader on its own moves more than 10% of the frame, towards more fog.

**Break demonstrations.** Each break below makes its named case fail, except H, which did not (see
the second list).
- **A:** ignoring `follow` in `fogLayerAltitude`.
- **B:** reading the ground at the ray's start for every piece.
- **C:** swapping `map0.zw` for `map0.xy`.
- **D:** taking the ground branch at follow 0.
- **E:** the march ignoring the follow.
- **F:** the march reading the ground at 0.
- **G:** a texel-centred bake.
- **H′:** removing the shader's terrain gate.
- **I:** the scene serialiser dropping the key.
- **J:** the parameter never reaching the environment.

**What the tests cannot see.**
- **A passed the first agreement test.** Both readers share `fogLayerAltitude`, so they went on
  agreeing about the wrong layer. That is why the CPU restatement of the profile was added.
- **H passed the render test.** H is the CPU no-terrain zeroing. The whole frame differed by an ULP,
  and the RGBA16F target rounded that away. That is why the gate is also in the shader, where the
  compute test compares values at f32.

## Consequences

- **Other consumers, noted and not migrated.** Water (the shoreline and the floaters' bed through
  `TerrainQuery`) and grass or scatter placement ask the CPU the same question this texture now
  answers. ADR-575 names both. Whoever owns those systems can bind `terrainHeightTex`; its CPU twin
  `TerrainGround::groundAt` is exact at grid points.
- **Nested scenes.** A nested scene's terrain is not propagated to the parent's `Scene::terrainGround`.
- **Merge note.** The only `volume.wgsl` changes are the `heightFog` comment and the ground branch in
  `volumeDensityAt`. The frame layout grew by one entry, at binding 12.
