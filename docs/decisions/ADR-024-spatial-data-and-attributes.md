# ADR-024: Spatial data — typed point attributes as the procedural currency

- Status: Accepted (2026-09-09)
- Research: `docs/research/spatial-data-and-attributes.md`, `procedural-world-systems.md`

## Problem
Procedural objects generate instance records directly from a distribution; nothing between generation and instancing can read or write per-point data, so fields, filters, attribute-driven generation and simulation have nowhere to attach.

## Decision
- `spatial::AttributeSet`: structure-of-arrays columns typed bool/int/float/vec2/vec3/vec4/colour, addressed by name, over a domain (Point now; Vertex/Primitive/Instance reserved). `AttributeView<T>` gives typed access; every column has `count` elements and compaction removes rows from all columns together.
- `spatial::PointCloud` = an `AttributeSet` with the conventional core columns always present: `position` (vec3), `rotation` (vec4 quaternion), `scale` (vec3), `id` (int), `seed` (int), `density` (float), `color` (colour), `velocity` (vec3), `normal` (vec3), `bounds` (vec3 half-extent). User attributes are added by name at any time.
- `SpatialData` is the umbrella: `PointCloud`, `Spline` (ADR-026), `Field` (ADR-025), `Sdf` (ADR-027), `Mesh` (existing `MeshData`), `Bounds`; each is a value type with a structural hash.
- The renderer's `InstanceRecord` is a fixed **projection** of a point cloud (`projectInstances(cloud)`): position, rotation, scale, index, four randoms (hashed from seed/id), colour, emissive. Optional `extra` vec4 lanes carry user attributes to shaders by name binding.
- Determinism: `id` and `seed` are stable per generator; all randomness is `hash(seed, id, channel)`; operations never depend on iteration order.
- Serialisation: generators and operator lists serialise; explicit clouds serialise as per-attribute JSON arrays with a type tag.

## Consequences
- Positive: any process (distribution, operator, effector, simulation, graph node) reads/writes the same columns; instancing, particles and SDF blobs can all consume a cloud.
- Negative: SoA columns cost an indirection per attribute; the GPU still consumes a fixed record, so exotic attributes need explicit lane binding.
