# ADR-1071: Per-material cel ("toon") lighting: bands, a shadow tone, a rim and a hard highlight

- **Status:** Accepted (2026-10-02), proto/sonic-garden (stylized engineering for the abstract direction,
  `docs/prototypes/sonic-garden/04-brief-abstract-direction.md`)
- **Code:**
  - `scene::ToonShading` and `Material::toon` in `src/scene/scene_types.hpp`;
  - the file block and the parameters: `src/scene/toon_shading.*`, called from `src/scene/procedural.cpp` and
    `src/scene/sdf_object.cpp`;
  - the packing: `src/rendering/toon_pack.hpp`, into `ObjectUniforms::toon0..2` (entity, procedural and SDF
    renderers);
  - the shading: the toon branch of `shadeSurface` (`shaders/pbr_shade.wgsl`), and `toonBand`,
    `shadeToonUnbanded`, `evaluateLightToon` and `lightVisibility` in `shaders/lighting.wgsl`.
- **Tests:** `tests/unit/test_toon_shading.cpp` (`[toon][adr1071]`), `tests/rendering/test_toon_gpu.cpp`
  (`[gpu][toon][adr1071]`).

## Context

The abstract direction asks for cel shading, flat shading, hard and posterised lighting. What existed was
`environment.stylized` (one painterly ramp for the whole scene, its constants in the shader) and material programs,
which can fake bands with a threshold on their own inputs but cannot see the lights or the shadows. The art agent
needs it per material, with every control a modulation target.

## Decision

A material carries a `toon` block. `bands` 0 (the default) is off; the gate is a uniform lane, so a material that
does not ask takes exactly the paths it took before (both other branches are untouched; the PBR branch's shadow
computation was moved into `lightVisibility` unchanged).

With `bands >= 1`, per light:
- the light's N.L, pushed to the shadow side by its shadow (`x = mix(-1, N.L, cut(visibility))`, the visibility
  itself cut at one half so PCF penumbrae and the contact march's jitter cannot print speckle in a flat tone), is
  measured from `terminator` and cut into `bands` lit tones, each edge `softness` wide. The brightest tone is
  albedo / pi times the light's radiance: what the PBR path gives a surface facing the light, so turning toon on does
  not change the exposure;
- `specular` > 0 adds a hard highlight: a disc where N.H passes `1 - size^2`, on the lit side only;
- every light kind is lit from one direction (an area light from its centre, at the brightness a surface facing it
  would get), because a band is a cut of one N.L.

Then, once per fragment: the shadow tone `albedo x shadowColor x ambient` (the ambient floor), and a rim -- a hard band
round the silhouette, `rimWidth` of a round object's radius wide (N.V below `sqrt(1 - (1 - w)^2)`), `rimColor x
rimIntensity`, added as emission so it blooms. Flat by design: no normal map, no IBL, no screen-space AO (its noise is
what a flat tone would print). The material program's colour and emission, the FXL lanes, the waves and the fog still
apply. The toon branch comes before the scene-wide styled path, so a toon material is toon whatever
`environment.stylized` says.

| parameter (under the owner's prefix) | file key | range | default |
|---|---|---|---|
| `toon/bands` | `bands` | 0..16 (0 off; rounded) | 0 |
| `toon/softness` | `softness` | 0.001..1 | 0.02 |
| `toon/terminator` | `terminator` | -1..0.99 | 0 |
| `toon/shadowColor` | `shadowColor` | colour | (0.45, 0.4, 0.7) |
| `toon/ambient` | `ambient` | 0..8 | 0.3 |
| `toon/rimWidth` | `rimWidth` | 0..1 (0 off) | 0 |
| `toon/rimColor` | `rimColor` | colour | (1, 1, 1) |
| `toon/rimIntensity` | `rimIntensity` | 0..100 | 1 |
| `toon/specular` | `specular` | 0..100 (0 off) | 0 |
| `toon/specularSize` | `specularSize` | 0..1 | 0.08 |

The owner's prefix is `procedural/<node>/` or `sdf/<name>/`. They are registered for every procedural node and SDF
object whether or not its file has a block, so the look can be switched on from the UI: select the object in the
World panel and the Inspector draws them as its **toon** section; in the Parameters panel they are the `toon` section
of the object's group. Every one is a route and timeline target; they are pure functions of the frame, so seek equals
play.

## Consequences

- `ObjectUniforms` grew from 464 to 512 bytes, its last three padding vec4s: it now fills its slot, and the next lane
  anyone needs grows `kObjectStride`. The layout guard checks the WGSL mirror. A pinned build and its own commit's
  shaders stay consistent; a NEW binary with OLD shaders (or the reverse) does not.
- Measured (M2 Max, 1920x1080, `docs/prototypes/sonic-garden/stylized-eng/bench-*.scene.json`, 180 frames):
  the scene pass 9.57 ms PBR, 7.93 ms with every material toon (cheaper: no IBL, no AO). Frame GPU p50 13.1 -> 11.1 ms.
- Not covered: glTF meshes' own materials (a node-level override is the natural next step); an SDF object's `look`
  occlusion and soft shadow multiply after the shading, so a flat SDF wants `look.aoStrength` 0; SDF surfaces
  (ADR-1044) share the object's toon; terrain and orb node materials.
