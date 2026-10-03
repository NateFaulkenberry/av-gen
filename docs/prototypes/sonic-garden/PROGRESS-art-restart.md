STOPPED 2026-10-02: realistic direction rejected by the owner; superseded by 04-brief-abstract-direction.md

# Sonic Garden art restart: progress (sonic-art)

Brief: `03-brief-art-restart.md` (the owner's words govern). Worktree `/Users/natefaulkenberry/Documents/GitHub/av-gen-sonic`,
branch `proto/sonic-garden`. Commit only my own paths (`git commit -- <paths>`), never `assets/`. Review media:
`~/Desktop/av-gen-review/27-sonic-art-restart/`.

## Resume here (cold)

- **State (2026-10-02 16:00):** step 1 (analysis) written: `ART-RESTART-ANALYSIS.md`. Engine capability survey in
  progress (terrain, water, glTF assets, effects). Next: the 16-place plan (`ART-RESTART-PLAN.md`), then blockouts in
  batches.
- **The pinned engine:** `96bc0214` (no engine change since: `git log 96bc0214..HEAD -- src shaders` is empty), at
  `$S/vfx/bin-96bc0214`, run through `$S/vfx/avgen.sh` (sets `AVGEN_SHADER_DIR` to the pin's shaders). Never render
  art from `build/release`.
  `S=/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/fed9412c-8e5e-42c0-a62b-e703644796ad/scratchpad`.
  If the scratchpad is gone, rebuild the pin: `git archive 96bc0214 | tar -x -C $S/vfx/src-96bc0214`, then
  `cmake --preset release -DCPM_SOURCE_CACHE=/Users/natefaulkenberry/Documents/GitHub/av-gen/.cache/cpm` and build
  the targets `avgen avgen_sonic_probe`; copy the binaries and `shaders/` into `$S/vfx/bin-96bc0214`.
- **The silent beauty test** is `tools/sonic_vfx/variant.py: make_silent` (routes, interpret and publish sources,
  triggered effects, live input, audio and notes removed; parameters, always-on effects and the timeline kept).

## Engine facts this pass builds on (verified 2026-10-02 against the source, with file:line in the surveys)

- **Placing assets:**
  - `{"kind":"gltf","asset":"../../assets/...","position","rotation"(deg),"scale"}`. A gltf node takes no
    `material` (it is dropped with a warning); the only overrides are `tint`, `roughnessScale`, `emissiveBoost`, the
    `nodes/<n>/opacity` parameter and `lod`.
  - To instance an asset, use a procedural node with `source {kind: mesh, asset, meshBudget}` and a distribution.
    Its `material`, if given, replaces the colour on every part but keeps the textures, and its default baseColor is
    purple.
  - Polyhaven files are a row of variants: one placement places the whole row.
  - Asset paths are relative to the scene file.
- **PBR mode only** for scans. `environment.stylized` drops texture RGB and the normal, roughness and AO maps.
- **Hard limits:**
  - 256 visible procedural objects (each material part and scatter layer counts);
  - 8 material programs;
  - node scale 0.001-100, `sourceTransform.scale` at most 100, procedural size at most 1000;
  - a procedural node with no `distribution` gives 32 copies on a ring.
- **Water exists only as `terrain.water`.** It appears in features with `water: true`, or below `seaLevel`. It
  reflects ONLY the environment cube: there are no planar reflections, SSR or probes. A mirror takes twin geometry.
- **There are no clouds, no caustics, and no shadowed god rays.** The fog march never samples shadow maps. A shaft is
  the `lightBeam` effect (it points down at tilt 0 and toward the horizon at +-89, never up). Placed `fog` media
  self-shadow (`volumeShadowSteps`).
- **Particles** are unlit, untextured (round or leaf), additive or alpha. `scatterStrength` makes them catch the key
  light.
- **Lights:**
  - types directional, point, spot, rect, disk, tube, sphere;
  - cones in degrees;
  - the shadow atlas holds 8 views (directional cascades, a spot 1, a point 6);
  - `contactShadow` dithers under a grazing sun.
- **Depth of field** is a project parameter (`post/dof/*`, `camera/lens/aperture`, `camera/focus/*`), not a scene key.
- **Terrain:**
  - `world {seed, size, baseHeight, erosion, seaLevel, layers[], features[], biomes[]}` and `terrain {chunkSize,
    resolution, viewDistance, water{}, flow{}}`, plus `scatter[]` layers (asset, densities per biome, height, tint,
    wind `motion`) and `clearings[]`.
  - An omitted world key keeps the shipped Glowmere value, so write `layers` and `features` out in full.
  - `heightImage` is parsed and does nothing.
  - The ground has no image textures: its colour comes from the biome colours or a material program.

## Engine needs (for the coordinator)

(none yet)

## Batch 1 blockout review (pass 3), 16:50

Sheet: `~/Desktop/av-gen-review/27-sonic-art-restart/work/blockout/batch1-blockouts.jpg`. The renders are silent, at
960x540, from `$S/ar/look`.

| scene | reads as a place? | verdict and next |
|---|---|---|
| cenote | yes: a cave, an opening, a shaft converging with the walkway on a figure in the light | the composition passes. Next: the water (flat teal panels), wall repetition, the opening's rim (roots, plants), plank detail, tint the figure |
| drowned-colonnade | not as UNDERWATER: the haze reads as air and the voronoi caustics as cracked mud | redesign the light: a bright rippling surface seen from below at the top of the frame, a deep blue cast, plankton, fish, sea grass; caustics thinner or dropped; a less static camera |
| lava-field | yes: a river of lava winding to a fountaining vent | the plain is smooth orange "sand": it needs rough basalt (rocks, ropy texture), finer and less regular cracks, the plume and the cones, and a scale cue (figures on a ridge) |
| wisp-marsh | yes, with the strongest mood of the four | the reeds are giant cartoon blades (wrong scale); the moon needs a halo; the tree twins for the mirror; the planks are too regular |

What batch 1 taught (applied from batch 2 on):
1. Put things ON the ground: `kit.terrain_heights(world, points)`.
2. In an enclosed space, use one shadow cascade: a near cascade's caster pull-back is capped at 3x its radius, so a
   roof 30 m up leaked sun onto the floor near the camera.
3. A shadowless fog lit by the sun washes a cave out. Give the sun `volumetric: 0` and draw the shaft as a lightBeam.
   Keep the point lights' `volumetric` small, and keep fog low with height fog so the sky stays clean.
4. `voronoiEdge` alone reads as tiles or cracks. Warp its domain, keep the lines thin, and modulate their strength.
5. Check asset sizes before scattering them. The Quaternius wispy grass at 0.9 came out 2-3 m tall.
6. lightBeam yaw 0 heads toward +Z. `kit.beam(..., toward=point)` solves tilt and yaw.
7. Do not let scanned geometry reach into clear water. The cliffs' bases showed through as dark shapes.

## Log

- 15:40 read the brief, the engineering notes and the old pass; the old stills and clips reviewed (16 of 16).
- 15:50 `ART-RESTART-ANALYSIS.md`.
- 16:00 `ART-RESTART-PLAN.md`: the sixteen places, written before any build.
- 16:10-16:50 the batch 1 blockouts: cenote, drowned-colonnade, lava-field, wisp-marsh (modules in
  `tools/sonic_vfx/scenes/`, built to `$S/ar/look` with `make.py <module> --out $S/ar/look`, rendered with
  `restart.py blockout <id> --projects $S/ar/look`).
- New kit pieces:
  - `gltf`, `figure`, `mesh`, `terrain`, `beam` and `fogbank`;
  - `terrain_heights` and `aim` (put a point at a chosen screen position);
  - absolute asset paths for builds outside `examples/`.
