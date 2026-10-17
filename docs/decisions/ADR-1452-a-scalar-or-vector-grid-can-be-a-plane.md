# ADR-1452: A scalar or vector grid can be a plane

**Status:** Accepted (proto/generative-rd)
**Date:** 2026-10-17
**Brief:** `docs/prototypes/generative-rd/00-brief.md` §3 (Experiment B: flow fields and advection).
**Implemented by:** `GridField::validate` in `src/spatial/grid_field.cpp` (the limits only; the kernels were already
dimension-agnostic).
**Tests:** `tests/unit/test_generative_grids.cpp`: "A scalar or vector grid one cell thick may be a plane up to 1024,
and steps as before" (the limits, and a plane's diffusion conserving mass with equal x and z neighbours).

## Context

Experiment B advects ink across a river's surface and keeps the eddies the boulders shed in a vector grid. Both are
surface phenomena: they need detail across the surface (hundreds of cells), not depth. A scalar or vector grid was
capped at 128 on every axis, which is a 1.25 m cell over a 160 m river. Ink at that resolution is a blur; the
semi-Lagrangian advection's own diffusion erases the marbling the brief asks for.

Agents, excitable media, oscillators and reaction-diffusion are already planes up to 1024 (ADR-1120, 1201, 1450,
1451).

## Decision

A `scalar` or `vector` grid with `resolution.y == 1` is a plane. x and z may each be up to `kMaxAgentGridResolution`
(1024). The table limit (`kMaxGridTableFloats`, 4M floats) still applies: a 1024 × 512 vector plane fits, a 1024²
one does not.

Nothing else changes:
- Injection, advection, diffusion and dissipation are gathers over 3D indices with the wrap rule. On a one-cell axis
  the y neighbours are the cell itself, so diffusion acts in x and z only and still conserves mass.
- A grid with `resolution.y > 1` keeps the 128 limit.

## Consequences

- **An ink plane at 512² over 160 m is a 0.31 m cell.** Its cost is that of the cells: one inject, one advect, the
  diffusion sweeps and one dissipate pass per sub-step.
- **Volumes are unchanged.**

## Rejected alternatives

- **Raising the volume limit.** 256³ is 16M floats, four times the shared table. A plane is what the use needs.
