# ADR-025: Fields as spatial control signals, effectors as field × operation, GPU-identical evaluation

- Status: Accepted (2026-09-09)
- Research: `docs/research/fields-and-effectors.md`, `gpu-procedural-execution.md`

## Decision
- `spatial::FieldSpec` (data): kind (scalar/vector/colour × type), transform, space (local/world), strength, falloff (`Falloff { kind, inner, outer, curve values, noise }`), animation (speed, phase), kind parameters. Scene-level list `Scene::fields` by name; composition node kind `field`; parameters `field/<name>/…`.
- `spatial::sampleScalar/Vector/Color(field, p, t)` on the CPU and `fields.wgsl` (`fieldScalar/Vector/Color(i, p, t)`) on the GPU implement the same maths (shared `fbm3`, curl by finite differences); the renderer packs fields into a uniform array (`FieldUniform`, ≤ 16) available to the procedural vertex shader, the particle simulate pass, the point-processing compute pass and material programs.
- Compound fields combine named children with add/multiply/max/min/mix.
- `spatial::Effector` (data): field name, operation (position offset, scale multiply/set, rotation, velocity, colour mix, density multiply, attribute write), blend, strength; `applyEffector(cloud, effector, fields, t)` on the CPU; the same list runs in `points.wgsl` for GPU clouds.
- Waves are a field kind (planar/radial/spherical/cylindrical travelling waves with envelope); their amplitude is a parameter like any other, so `audio.onset → field/wave/amplitude` is an ordinary route.
- Field → geometry: an effector on the object's point cloud (instances), a `Field` deformer kind for per-vertex displacement, and material inputs; field → particles: `fieldForces` entries on a particle system; field → lights: light intensity/position from field samples (ADR-031 follow-up).

## Consequences
- Positive: one spatial signal drives instances, vertices, particles, materials and volumes; audio reaches fields through parameters only.
- Negative: 16 field slots per frame; simulated/texture fields need a binding slot (ADR-032); CPU sampling of large clouds is bounded by the GPU path being available.
