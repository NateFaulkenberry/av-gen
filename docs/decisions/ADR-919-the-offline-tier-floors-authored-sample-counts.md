# ADR-919: The offline tier floors the sample counts a scene authored

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-147 (the tier a deliverable is rendered at), ADR-139 (the volume's two scalability
axes), ADR-036 (the procedural sky's IBL), ADR-577 (the march's grain against its step count)
**Found by:** the GV3 revision audit, `docs/glowmere-valley-3/revision/audit/reports/render-post.md`
(engine gap 7)
**Implemented by:** `QualitySettings::volumeStepFloor`, `textureAnisotropy` and `skyCubeFloor`
(`src/rendering/render_quality.hpp`); `VolumeRenderer::update`; `SamplerCache::setMaxAnisotropy`
(`src/gpu/texture.{hpp,cpp}`) applied by `SceneRenderer::setQualitySettings`;
`SceneRenderer::updateEnvironment`; the Render section's tier row (`src/ui/control_panel.cpp`)
**Tests:** `tests/rendering/test_offline_floors_gpu.cpp` (`[gpu][quality][offline]`)

## Context

A tier scales what a scene authored: offline's `volumeStepScale` is 1, so offline takes exactly the
scene's steps. A scale can never make a final better than the preview it was tuned on. The audit
found three sample counts the offline tier left as authored:

- **The volumetric march:** GV3 authors 12 steps. ADR-577 measured the march's animated grain at 1%
  of a thick medium's brightness at 16 steps and 1e-4 at 32.
- **Texture filtering:** every material sampler was made at a fixed 8x anisotropy
  (`SamplerCache::get`).
- **The visible sky:** a scene lit by the procedural sky draws its background from the prefiltered
  cube's first mip (skybox.wgsl), 128 px a face from a 256 px source. Behind a 40 degree lens a
  2160-line frame spans a texel of that sky over about 38 px, and the linear interpolation between
  texels leaves creases in the horizon's gradient that read as bands.

## Decision

**The offline tier raises what a scene authored below three floors, and leaves alone what is
above them.** A floor changes how finely the picture is sampled, never what it is.

| Floor | Offline | Every other tier |
|---|---|---|
| `volumeStepFloor`: the march's steps a ray, when the march runs | 32 | 0 (none) |
| `textureAnisotropy`: material textures' anisotropic filtering | 16x | 8x (as before) |
| `skyCubeFloor`: the procedural sky's source and prefiltered cubes, texels a face | 1024 | 0 (256 and 128) |

- **The march:** `steps = max(authored x scale, floor)`. A scene that authored 48 keeps 48.
  `VolumeStats::authoredSteps` records the count before the floor. ADR-570's self-shadow steps are
  not floored: they are part of the look, and the ADR declined to let a tier touch them.
- **Anisotropy:** `SamplerCache` takes the tier's value (clamped to WebGPU's 1..16). A change drops
  the cached samplers and the material bind groups made from them.
- **The sky:** both cubes are floored, because the prefiltered cube's first mip is the visible sky.
  The sizes are part of the sky's rebuild key, so a tier change rebuilds it. A scene lit by an HDRI
  draws the map itself at its own resolution (ADR-049) and is not affected.
- **It says what it raised,** once, where it takes effect:
  - "the tier's floor raised the volumetric march from 12 to 32 steps a ray"
  - "texture filtering at 16x anisotropy (the tier's floor raised it; it was 8x)"
  - "the tier's floor raised the procedural sky's cube from 256 to 1024 px a face and its prefiltered
    cube -- the visible sky -- from 128 to 1024"
- **Where it is seen:** the Render section's tier row says, when offline is chosen, "raises, never
  lowers: fog at least 32 steps a ray, textures filtered at 16x, the sky drawn from 1024 px a face",
  read from the tier table. The tier's tooltip says the same. The floors are adjusted as a set by
  choosing the tier.

## Consequences

- **Every offline render changes where a floor applies:**
  - scenes whose march runs with fewer than 32 steps: 51 of the 58 shipped scene and project
    files whose march runs, 32 of them at 12 steps (a census of `examples/`);
  - every textured material seen at a grazing angle steeper than 8:1;
  - every scene lit and backed by the procedural sky: its visible sky is sharper, and specular
    reflections of it at low roughness are too.
  - Realtime, High and Preview renders, and the editor, are unchanged.
- **Build cost:** the sky is built once per render: 44 ms at 1024 px a face against 15-20 ms at
  256 (GV3, logged by `processSky`).
- **Measured on GV3** (a snapshot; main `3f720bfa` as the before arm): its 960x540 x2 preview at
  the offline tier changes in **4,662 of 518,400 pixels (0.9%)**, 85% of them by one code value
  (the finer sky's gradient crossing rounding boundaries) and 80 by ten or more (textures at grazing
  angles). Two seconds of s14 at 3840x2160 x2 cost the same, 41.0 s, with the floors and without.
- **GV3:** its march is off (`scene/volumeMaxDistance` 0), so the step floor only matters if the
  revision turns the march on, which the audit recommends (220 m). Its textures and its sky take the
  other two floors in every offline render, previews included, because its project renders at the
  offline tier.
- **Measured** (`test_offline_floors_gpu.cpp`):
  - The march: a scene authoring GV3's 12 steps marches 32 at offline (`VolumeStats::authoredSteps`
    12, `steps` 32) and 12 at realtime; one authoring 48 keeps 48 at offline.
  - Anisotropy: a checkerboard floor at a grazing angle, one tier, only the setting moved: 16x
    changes 3666 of 64000 pixels against 8x, and 8x twice is the same picture to the byte.
  - The sky: a column through the horizon band at a 2160-line x2 frame's pixel density (4.75
    degrees over 512 px), against the analytic sky it was built from, after dividing out the common
    scale -- worst deviation **0.171 at 128 px a face, 0.00199 at 1024** (86x), RMS 0.0322 and
    0.000586 (55x). The coarse cube's error is a row of arches between texel centres, the shape an
    8-bit gradient shows as bands. (The first version of this test also scored the column's slope
    jumps; at this gradient that statistic was RGBA16F quantisation, 1.74 and 1.39, and it was
    replaced.)
