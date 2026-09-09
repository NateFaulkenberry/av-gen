# Particle rendering, trails and motion

Status: research (2026-09-09). Decision: ADR-040.

## 1. Where the current system stands

GPU simulation with deterministic compaction, field forces, curl noise, attractors, and rendering
as camera-facing quads with a depth fade. What is missing for cinematic work: trails and ribbons,
velocity-aligned stretching, size and colour curves over lifetime beyond a linear ramp, motion
vectors, and any integration with the atmosphere.

## 2. Trails and ribbons

Two established approaches:

- **History buffer per particle**: keep the last N positions in a ring, build a ribbon each frame
  from the history. Memory is `capacity * N * 16` bytes, which bounds N sharply at large counts,
  but the geometry is stable and easy to texture along its length.
- **Stretched billboards**: extend the quad along the velocity vector by `velocity * shutter`. Free
  in memory, one vertex-shader change, and enough for sparks, rain and fast debris. It is what most
  real-time engines use for high counts.

Both are worth having: stretched billboards as the default for large counts, ribbons for the
smaller hero emitters where the shape of the path matters (the reassembly scene's parts, the
machine's energy arcs).

## 3. Motion vectors

Particles need previous positions to write velocity. The simulation already double-buffers state;
exposing the previous position lets the draw write a velocity target, which then feeds motion blur
and temporal reprojection. Without it, fast particles either strobe or must be blurred by cheating.

## 4. Integration with atmosphere

Particles that ignore the fog sit on top of the image. Two fixes: sample the same volumetric
transmittance the fog pass computes when shading a particle, and let bright particles inject light
into the volume (an emissive term the volume march reads). The second is what makes sparks light
the dust around them, which is one of the strongest "this is a real space" cues available.

## 5. Determinism

Everything above must stay deterministic: history buffers are part of the simulation state, and
the stretch factor is a function of the simulated velocity, not of wall time. The existing fixed
sub-step model already guarantees this as long as no new source of randomness is introduced.
