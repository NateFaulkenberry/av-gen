# ADR-1966: Tile cloud layers -- omit the water cells, opt out of the dynamics and the contours

- Status: Accepted (Musical Geography CP-D r2, request CPD-R2, 2026-11-10), scene/musical-geography.
- Code:
  - `GeneratorSpec::omitWater`, `dynamics`, `contours` (`src/scene/generator.hpp`), read and written by
    `src/scene/procedural.cpp`;
  - the kernel's empty record for a wet cell (`cs_generate_tiles`, `shaders/generator.wgsl`);
  - the packing and the contour dispatch (`src/rendering/procedural_renderer.cpp`).

## Context

The art agent's cloud layers are second `tiles` grounds lifted to altitude (`02-requests.md`, CPD-R2). The layer's
continent field is the cover, and a material program cut the "sea" with `opacity = 1 - water` and an alpha mask. Every
cell rasterised. The empty half ran the masked program in the depth prepass and then discarded it, so two layers cost
+44 ms of GPU at 1080p ultra. The same layers drawn opaque cost +7.6 ms.

## Decision

1. **`"omit": "water"`** on a tiles generator (default `"none"`). A cell whose water channel is at least 0.5 is written
   as an empty, zero-scale record. That is what a ring's hole already is (ADR-1976): the cull pass rejects it from the
   camera and shadow lists (ADR-1117), so it costs no vertex, raster or fragment work. The cloud material drops its
   opacity and alpha mask, and draws opaque.
2. **`"dynamics": false`** stands the layer on the static ground. The scene's `tileDynamics` (waves, breathing, rolling,
   rings and landing springs) never reaches it; the renderer packs it with an inactive `TileDynamics`.
3. **`"contours": false`** skips the layer's contour pass (ADR-1970) and leaves its contour bits at 0.
4. **`omitBelow`** (metres, generator space): a cell whose top is below it is also written empty. This is a cloud
   layer's night "cores only" cut, made per cell instead of per pixel. A keyable parameter,
   `procedural/<node>/distribution/generator/omitBelow`, registered for tiles generators only; at or below −10000
   (the default) it is off.

The first three are structural generator fields, not parameters; all four are hashed.

**A keyed transform keeps the window about the camera.** The composition writes a node's transform into the
generator's `distributionTransform` every frame. The window is then computed from the camera in generator space
(`generatorCameraSpace`). So a node whose position is keyed (the wind drift) carries its clouds downwind while the
window stays centred on the camera. This was already true; R2 adds a test.

## Consequences

- Two layers at 1080p ultra, measured with the live profile at 40-60 s of the full song (GPU p50):
  - no clouds: 16.48 ms;
  - masked: 119.1 ms (depth prepass 93 ms);
  - `omit: "water"`, opaque: 17.13 ms, **+0.65 ms**, under the 2 ms target.

  The opt-outs measured no saving (17.17 ms with them left on). The table is in `02-requests.md`.
- At night (140-160 s):
  - the opacity cores cut kept on the remaining cells as an alpha mask still costs +16.6 ms, because the masked
    prepass is the cost;
  - the same cut as `omitBelow` costs **+0.11 ms**.
- A layer that opts out of the dynamics no longer slides with the ground's rolling. Drift comes from a keyed node
  position.
- Cloud shadows on the ground (the nice-to-have) are not built.
- Tests: `test_tile_clouds_gpu` [adr1966]:
  - a deck as a cloud layer: no wet cell is present and every dry cell is; it stands on the still ground under the
    scene's waves; no contour bits;
  - the same deck as authored moves and has contours;
  - a keyed node position (0, 137, 941 m) keeps the window the one about the camera;
  - a keyed `omitBelow` (off, 2 m, 6 m) leaves exactly the dry cells at or above it.
