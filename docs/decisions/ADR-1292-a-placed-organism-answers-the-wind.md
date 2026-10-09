# ADR-1292: A placed organism answers the wind

- Status: Accepted (2026-10-09), proto/bio-valley (macro showcase, owner note 5)
- Code: `src/scene/procedural.cpp` (`ProceduralGeometry::fromJson` / `toJson`: a `motion` block).
- Tests: `tests/unit/test_generator.cpp` (`[adr1292]`).

## Context

`ProceduralGeometry::motion` (ADR-055's VegetationMotion: stiffness, mass, damping, windSensitivity, tip amplitude,
bend limit and curve, gust response) drives the wind sway the procedural renderer already evaluates per frame. Only
an ecology scatter layer could set it. A procedural node placed by hand -- every node of a curated library -- had no
way to say how it moves, so in a windy scene the whole macro stood still.

## Decision

A procedural node may carry `"motion": {...}` with VegetationMotion's keys (core/wind.hpp `motionFromJson`).
Stiffness and mass must be positive; damping, sensitivity, tip amplitude and bend limit must not be negative. It is
written back only when `windSensitivity > 0`. Absent, nothing changes: sensitivity 0 is still the default.

## Consequences

- The sway is the existing analytic oscillator (ADR-055): a pure function of the timeline second and position, so
  seek equals play.
- The macro gives each species its own stiffness and mass, so their resonances and phase lags differ and a gust
  front travels through the scene rather than moving everything in step.
