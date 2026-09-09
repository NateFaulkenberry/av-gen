# ADR-026: Splines as first-class spatial data

- Status: Accepted (2026-09-09)

## Decision
- `spatial::Spline` (data): kind (polyline, Catmull-Rom, Bezier, Hermite), control points (position, optional tangents), closed flag, samples count; `sample(t)`, `sampleByDistance(d)`, `tangent(t)`, `normal(t)` (parallel-transport frames, rotation-minimising), `length()`, arc-length table built lazily and cached by hash.
- Generators: line, circle, spiral, helix, bezier, noise (jittered), plus "from points" (a cloud's positions in id order).
- Uses: `Distribution::kind = Spline` places instances by distance or count with frame orientation and roll; a `Path` deformer kind deforms the source along a spline (curve deform: the source's axis coordinate maps to distance, its cross-section to the frame); particles emit along splines (`EmitterShape::Spline`); lights along splines; camera mode `spline` (`camera/splineT`, look-ahead, roll).
- GPU: splines are uploaded as sample tables (position, tangent, normal, distance; 256–1024 samples) in a storage buffer; the vertex shader interpolates for path deformation.

## Consequences
- Positive: corridors, helices, rails and camera paths from one representation; audio-drivable spline parameters.
- Negative: parallel-transport frames are per-spline-sample, so a spline edited every frame recomputes its table (cheap for ≤ 1024 samples).
