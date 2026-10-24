# ADR-1660: The aurora's ribbons can fill the sky

- Status: Accepted (scene/aurora-borealis)
- Extends ADR-1641 (the aurora as ribbons overhead). Uses a lane ADR-939 left free (`AuroraGpu::audio3.y`), so no
  struct grows.
- Asked for by the Aurora Borealis live scene (`docs/prototypes/aurora-borealis/`): the brief makes the aurora the
  scene's hero, filling about the upper half of every frame.
- Code:
  - `src/world/atmospherics.{hpp,cpp}`: `AuroraShape::ribbonCoverage` (0..1, default 0), its validation, packed into
    `audio3.y` only while ribbons are on.
  - `src/world/effects/kinds/aurora_effect.cpp`: the row (Ribbons > Coverage) and its JSON, `shape/ribbonCoverage`.
  - `shaders/atmosphere_fx.wgsl` (`atmosAuroraRibbons`): the gap noise's two thresholds are lowered by
    `0.32 x coverage`. `shaders/common.wgsl`: the lane's comment.
  - The three shipped projects whose saves carry an aurora gain the key at 0, as ADR-1641 did.
- Tests: `tests/unit/test_aurora_audio_response.cpp` (packing, the lane is zero while ribbons are off, validation,
  JSON round trip, a legacy file reads 0); `tests/rendering/test_aurora_ribbons_gpu.cpp` (`[adr1660]`: coverage 0 is
  ADR-1641's sky hash for hash; coverage 1 lights over a quarter more of the sky and still leaves dark sky between
  the sheets).

## Context

The ribbons' gaps come from a low-frequency noise of the footprint, `smoothstep(0.22, 0.55, fbm(q * 0.45))`, with
`q = world xz / fold size + drift`. Its cells are about 2.2 folds across, 10 km at the fold sizes that keep the
sheets crisp from a low camera (ADR-1641's sheet widening grows with `plane spacing / fold size`).

Measured on the Aurora Borealis blockout (stills in `~/Desktop/av-gen-review/aurora-borealis/`, the session's
`r6`-`r9` probes): with the camera travelling a 2.6 km loop under such a sky, the visible aurora changed from one
broad arch across the frame to an empty sky and back within a minute, whatever fold size, altitude or curtain height
was chosen. A smaller fold size averages more cells but widens every sheet into a blur at the elevations a valley
camera sees (0-30 degrees). No combination of the existing settings gives crisp sheets and a sky that is reliably
full, and a second aurora effect for coverage would double the ribbons' per-sky-pixel cost (about +5 ms at 1080p,
01-research-scene.md section 3.2).

## Decision

One setting, `ribbonCoverage`, lowers both thresholds of the gap noise. At 0 the arithmetic is ADR-1641's exactly
(the GPU test holds it to the same hash). At 1 the presence is above about a third almost everywhere, so the sheets
-- still cut by their own mid-level line, still separated by dark sky -- reach across nearly the whole sky.

It costs nothing measurable directly; a fuller sky early-outs less, which is the ribbons' known cost driver (ADR-1641)
and is measured in the scene's live profile.

## Consequences

- A scene can keep a large fold size (crisp sheets) and still have the aurora in every frame.
- The ADR-1652 pulsed copy of the ribbon march (eng/aurora-borealis) must take the same two-threshold change when it
  merges, or a pulse would change the coverage while it runs. Its own comment says a change to the ribbons belongs in
  both functions.

## Rejected

- **Two aurora effects** at different fold sizes: the coverage, at twice the cost.
- **A smaller fold size**: more cells in view, but the sheets blur at low elevation (widening is relative to the
  fold).
- **Translating the world to a lucky region of the noise**: the drift moves it anyway, and live time is unbounded.
