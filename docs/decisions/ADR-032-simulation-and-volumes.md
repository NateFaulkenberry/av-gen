# ADR-032: Simulated fields and volumetric atmosphere

- Status: Accepted (2026-09-09)
- Research: `docs/research/spatial-simulation.md`

## Decision
- **Grid fields**: `spatial::GridField` (2D/3D scalar or vector, resolution, bounds) as a field kind backed by a 3D texture / storage buffer; simulation steps (advect, diffuse, project, inject from fields/particles, reaction–diffusion) run as compute passes with a fixed sub-step and fixed iteration counts; the field is sampled through `fields.wgsl` like any other kind. First implementation: 2D/3D scalar with injection + diffusion + advection by a vector field, and Gray–Scott; full fluid projection later.
- **Volumetric atmosphere**: `Environment` gains height fog (density, height, falloff), volumetric fog (intensity, scattering, absorption, noise scale/speed, steps) and an optional density field name; `rendering::VolumeRenderer` raymarches the fog after the lit pass at half resolution with depth-aware upsampling, lit by the key light and emission from a colour field. Parameters `scene/fog*`, `scene/volume*`.
- Determinism: fixed steps, gather-only kernels, seeds explicit; offline and live share the render-time model.

## Consequences
- Positive: atmosphere and simulation become spatial signals for the rest of the system.
- Negative: memory for 3D grids (128³ × 16 B = 32 MB); volumetrics cost 1–3 ms at half resolution; no shadows in fog yet.
