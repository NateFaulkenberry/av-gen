# ADR-040: Particle trails, velocity-aware rendering and motion blur

- Status: Accepted (2026-09-09)
- Research: `docs/research/particles-and-motion.md`

## Decision
- **Stretched billboards**: particles optionally extend along their velocity by
  `velocity * shutter * stretch`, with a cap. Default for large counts, no extra memory.
- **Ribbons**: an opt-in per-system history buffer of N previous positions (N up to 32) drawn as a
  camera-facing ribbon with width and colour curves along its length. Bounded by capacity times N,
  so it is reserved for hero emitters and validated against a memory budget.
- **Curves over lifetime**: size, colour and opacity become small keyframed curves rather than a
  linear start-to-end ramp, evaluated in the shader from a compact packed representation.
- **Motion vectors**: the simulation already keeps the previous position; the draw writes screen
  velocity into the velocity target, which feeds motion blur and temporal reprojection.
- **Motion blur** becomes tile-based reconstruction over the velocity target (camera, object,
  instance, deformation and particle motion), replacing the depth-reprojection camera-only blur.
- **Atmosphere integration**: particles sample the volumetric transmittance when shading, and
  emissive particles inject light into the volume so sparks illuminate the dust around them.
- Determinism is preserved: history is part of simulation state, stretch derives from simulated
  velocity, and no wall-clock randomness is introduced.

## Consequences
- Positive: fast motion reads as motion; sparks and debris belong to the space rather than sitting
  on it; the reassembly showcase becomes expressible.
- Negative: ribbons cost memory and a second draw path; tile-based motion blur adds two passes;
  volume injection couples the particle and volume passes in one direction.
