# Spatial simulation fields

Status: research (2026-09-09). Decision: ADR-032.

## 1. Grid fields
- 2D/3D scalar and velocity grids in storage buffers or 3D textures; sampled trilinearly. Stam, "Stable Fluids" (SIGGRAPH 1999): semi-Lagrangian advection, diffusion by implicit relaxation, projection to divergence-free velocity; unconditionally stable, deterministic with fixed step and fixed iteration counts. (https://www.dgp.toronto.edu/public_user/stam/reality/Research/pdf/ns.pdf — accessed 2026-09-09.)
- Harris, GPU Gems ch. 38 (fluid on the GPU): ping-pong textures, Jacobi iterations. (https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu — accessed 2026-09-09.)
- Reaction–diffusion (Gray–Scott): two scalar grids, feed/kill parameters, explicit Euler with a small step; the canonical "organic" pattern generator. (Pearson, "Complex Patterns in a Simple System", Science 1993.)
- Curl noise (Bridson 2007) as an analytic stand-in for turbulence before any grid exists.

## 2. Determinism
Fixed time step per render frame (sub-stepped to a maximum dt), fixed iteration counts, no atomics in accumulation (use gather, not scatter), all seeds explicit. Offline and live share `renderTime`; a simulation state is a function of (initial state, sequence of dt, inputs); with a fixed step it is a function of frame index. Audio inputs come from the analysis frame for that render time in both modes (ADR-012).

## 3. Integration path
Simulated fields implement the same field interface (`sample` on CPU by readback for tests, GPU sampling in `fields.wgsl` through a 3D texture binding), drive particles (advect), geometry (effectors, displacement), materials (density → colour/emission) and volumes (density = the scalar grid). Notch's field/affector model is the closest reference for this coupling. (https://manual.notch.one/2026.1/en/docs/nodes/fields/ — accessed 2026-09-09.)
