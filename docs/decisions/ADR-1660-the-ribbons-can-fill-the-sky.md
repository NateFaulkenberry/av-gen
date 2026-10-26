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
  - `shaders/atmosphere_fx.wgsl` (`atmosAuroraRibbons`): the sheet stands on the footprint's level
    `0.5 - 0.125 x coverage` instead of 0.5. `shaders/common.wgsl`: the lane's comment.
  - The three shipped projects whose saves carry an aurora gain the key at 0, as ADR-1641 did.
- Tests: `tests/unit/test_aurora_audio_response.cpp` (packing, the lane is zero while ribbons are off, validation,
  JSON round trip, a legacy file reads 0); `tests/rendering/test_aurora_ribbons_gpu.cpp` (`[adr1660]`: coverage 0 is
  ADR-1641's sky hash for hash; coverage 1 leaves less bare sky and puts out nothing that was lit).

## Context

A ribbon is the level line `fbm(q) = 0.5` of a two-octave footprint noise (`q = world xz / fold size + drift`). That
noise averages 0.375 and peaks at 0.75, so its 0.5 line is a handful of closed loops round the noise's peaks: most of
the sky has no ribbon in it.

Measured on the Aurora Borealis blockout (the session's probes; stills in the review folder): with the camera
travelling a 2.6 km loop, the visible aurora went from one broad arch to an empty sky and back within a minute, at
every fold size, altitude and curtain height tried. A smaller fold size puts more loops in view but widens every sheet
into a blur at the elevations a valley camera sees (ADR-1641's widening is relative to the fold). A second aurora
effect would double the ribbons' per-sky-pixel cost (about +5 ms at 1080p, 01-research-scene.md s3.2).

A first attempt lowered the thresholds of the separate low-frequency gap noise instead. It changed almost nothing,
which showed that the gaps are mostly the level line's, not the gap noise's.

## Decision

One setting, `ribbonCoverage`, moves the sheet's level from 0.5 toward the noise's mean (0.375 at 1). Near the mean
a level line is long and winds through the whole footprint, so the sheets reach across the sky; they are still cut
by the same width, so dark sky stays between them. At 0 the arithmetic is ADR-1641's exactly (the GPU test holds it
to the same hash).

It adds one multiply-add. A fuller sky early-outs less, which is the ribbons' known cost driver; the scene's live
profile measures it.

## Consequences

- A scene can keep a large fold size (crisp sheets) and still have the aurora in every frame.
- The ADR-1652 pulsed copy of the ribbon march (eng/aurora-borealis) must take the same level change when it
  merges, or a pulse would change the coverage while it runs. Its own comment says a change to the ribbons belongs in
  both functions.

## Rejected

- **Two aurora effects** at different fold sizes: the coverage, at twice the cost.
- **A smaller fold size**: more cells in view, but the sheets blur at low elevation (widening is relative to the
  fold).
- **Translating the world to a lucky region of the noise**: the drift moves it anyway, and live time is unbounded.
- **Lowering the gap noise's thresholds** (the first attempt): measured to change almost nothing.
