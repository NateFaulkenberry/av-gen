# ADR-033: Clustered forward lighting, area lights, colour temperature, light rigs

- Status: Accepted (2026-09-09)
- Research: `docs/research/cinematic-lighting.md`

## Problem
Eight punctual lights in one forward pass, no area lights, no shadows, no temperature, and no way
to reuse a lighting setup. Lighting cannot establish hierarchy, mood or form.

## Decision
- **Light data** (`scene::Light`, replacing `PunctualLight` and keeping its JSON readable):
  type (directional, point, spot, rect, disk, tube), position, direction, up, colour, intensity in
  photometric units, `temperature` (Kelvin) and `tint`, size (`width`/`height` for rect, `radius`
  for disk/tube/sphere), range, cone angles, plus per-light flags `castsShadow`,
  `contactShadow`, `volumetricStrength`, `diffuseOnly`/`specularOnly`, and a `role` tag
  (key, fill, rim, back, ambient, practical) used by rigs and by the inspector.
- **Clustered forward**: a compute pass builds a froxel grid (16x8x24 by default) of light indices
  each frame; `pbr_shade.wgsl` reads only its cluster. Meshes, procedural instances and SDF
  surfaces keep sharing one shading function. The 8-light uniform path remains as the fallback tier
  when the cluster pass is disabled, so nothing regresses.
- **Area lights**: linearly transformed cones for rect and disk (Heitz et al. 2016) with the
  precomputed matrix table shipped as a small texture; representative-point approximation for
  tube and sphere. Area lights are the default for anything that should read as soft.
- **Colour temperature**: Planckian locus to linear sRGB, multiplied with the light's colour on the
  CPU when packing, so the shader stays unchanged and the value is inspectable.
- **Light rigs** (`scene::LightRig`): a named list of lights expressed in a frame relative to the
  subject and the camera (azimuth, elevation, distance in subject radii) plus intensity ratios,
  expanded into ordinary lights at build time. Rigs are data files under `examples/lightrigs/`,
  selectable per world and morphable through presets.
- Every field is a registered parameter (`light/<name>/…`, `lightrig/<name>/…`).

## Consequences
- Positive: hundreds of lights become affordable; softness and warm/cool relationships become
  first-class; a world can be relit by swapping a rig.
- Negative: a cluster build pass per frame; LTC needs a lookup texture; the light struct grows and
  its JSON gains fields (old files still load, since every new field has a default).
