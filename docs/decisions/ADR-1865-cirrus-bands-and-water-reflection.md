# ADR-1865: Cirrus bands, and water that reflects the cirrus

- Status: Accepted by the owner (2026-10-29), scene/strange-attractor (canyon cirrus follow-up, 2026-10-28). Extends ADR-1864.
- Code:
  - `SkyCirrus::bands` and `SkyCirrus::bandWidth` (`src/scene/scene_types.hpp`).
  - `scene::skyCirrus` and `packSkyCirrus` (`src/scene/sky.{hpp,cpp}`). The pack now has a fifth vec4: bands, band
    width, the cube's share, and the sky's intensity.
  - `shaders/sky_cirrus.wgsl`.
  - `EnvUniforms::cirrus4` and `FrameUniforms::skyCirrus4`.
  - `shaders/water.wgsl`: the reflection block (`WATER_CIRRUS_FLATTEN`).
- Tests:
  - `[adr1865]` in `tests/unit/test_sky.cpp`: bands 0 is the even layer exactly; bands only remove cloud, leaving less
    cover and more clear sky.
  - `tests/rendering/test_water_cirrus_gpu.cpp`: on a glass sheet, the pixels the layer brightens are the ones whose
    *mirrored* view ray the CPU twin puts under a streak. Fewer than 2% disagree.
- Evidence: `docs/prototypes/strange-attractor/09-canyon-cirrus-2.md`.

## Context

The owner reviewed ADR-1864 in the canyon and raised two things.

**The wide sky was too regular.** At 33 s and 40 s there were "too many evenly spaced parallel streaks" that read as
hatched. The layer's single noise field has one scale across the streaks, and the plane projection packs it ever
closer toward the horizon.

**"Shouldn't the clouds reflect in the river?"**
- The river's sky reflection is the IBL cube at a roughness mip, plus ADR-1650's sky mirror. The mirror reads the
  frame copy only where the reflected direction's vanishing point is on screen, which for a river under a camera
  looking down a canyon is mostly above the frame.
- The cube carried 15% of the layer, which is a faint shimmer, because a cube face cannot hold a streak. At full
  strength the streaks had come out as blotches.

## Decision

**Bands.** A low-frequency field gathers the streaks into a few broad bands of uneven width, spacing and brightness,
with clear sky between them.
- The field is two octaves of the same value noise, at `bandWidth` across the streaks and 3 × `length` along them.
- Cover is multiplied by `1 − bands × (1 − smoothstep(0.42, 0.62, b))`.
- Brightness is scaled by `1 + bands × (0.6b − 0.3)`.
- `bands` 0 is ADR-1864's layer bit for bit. The canyon uses 1 with a 5 km band width.

**The water evaluates the layer along its reflected ray**, using the visible sky's own function, after the cube sample.
- **Exact cover.** The cube already holds `share` × the cover (blurred). Mixing toward the streak colour by
  `k = c (1 − share) / (1 − share × c)` brings the cube's partial cover up to the full cover `c` exactly, with no
  double counting.
- **Flattened normals.** The ray uses the surface normal eased 60% toward flat (`WATER_CIRRUS_FLATTEN`). The swell
  still bends the streaks, but the finest ripples would otherwise shatter them into sparkle: a streak is a hair's
  width of reflected angle.
- **Aliasing fade.** The layer fades where the reflected direction changes by more than 0.02–0.06 rad across one
  pixel (`fwidth`), the scale at which a streak would alias.
- **Near-mirrors only.** The layer fades on rough water (roughness 0.1 → 0.4), where the cube's share is the right
  answer.
- **Scope.** Only when the IBL is the procedural sky and the sky has a layer, so no other scene changes. The sky mirror
  and SSR still win where they apply. They show the frame's own sky, which carries the same layer.

The sky cube keeps `reflection` (the share) for the diffuse lighting and for rough reflections.

## Consequences

- Any water under a cirrus sky reflects the streaks, aligned with the sky above them.
- Cost: one cirrus evaluation per water pixel. Measured at about 1.5 ms of the canyon's 1080p ultra scene pass, where
  the river covers much of the frame.
