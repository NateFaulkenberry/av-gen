# Fields and effectors

Status: research (2026-09-09). Decision: ADR-025.

## 1. Definitions used by the tools

- **Notch**: a Field is a volume of colour + motion vectors; Field Affectors (primitive, turbulence, curl noise, vortex, point, procedural, mesh) modify it; particles, cloners and deformers read fields. (https://manual.notch.one/2026.1/en/docs/nodes/fields/ — accessed 2026-09-09)
- **Cinema 4D MoGraph** (the origin of the term): Effectors change clone parameters (position, rotation, scale, colour, weight) with a *falloff* (now "Fields": shapes, noise, formula, layered with blend modes); the falloff defines *where* and the effector defines *what*. (https://help.maxon.net/c4d/ — "Fields", accessed 2026-09-09)
- **Houdini**: volumes/VDBs sampled with `volumesample`; "attribute from volume"; VEX noise functions; POP forces (attract, vortex, curl noise, wind) act on particles.
- **Unreal Niagara**: force modules (curl noise, vortex, point attraction) and *data interfaces* for sampling vector fields and SDFs. (https://dev.epicgames.com/documentation/en-us/unreal-engine/niagara-overview-for-unreal-engine — accessed 2026-09-09)

## 2. Abstraction

`sample(p, t) -> value` where value is scalar, vector or colour; plus a **falloff** (inner/outer radius, curve) and a **transform** (local/world). Composition: a compound field combines child samples with add/multiply/max/min/lerp — that is what "layered fields" are in C4D and what field affectors accumulate in Notch.

Analytic kinds (cheap, GPU-identical): constant, linear gradient, radial, box, sphere, plane, noise (fBM), Voronoi (F1 cellular), distance-to-point/line/plane, SDF distance (from the SDF tree, ADR-027); vectors: constant direction, radial (out/in), attractor/repulsor (falloff-weighted), vortex (axis × radial), curl noise (Bridson et al. 2007, https://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph2007-curlnoise.pdf), spiral (vortex + radial), wave (planar/radial/spherical/cylindrical travelling waves); colours: constant, gradient, radial gradient, noise colour, position colour.

Backed kinds (later): 2D/3D texture volumes, simulated fields (ADR-032).

## 3. Falloff

Distance from the field's local origin (or along its axis for gradients) mapped through a curve between `inner` (full) and `outer` (zero): none, linear, smoothstep, smooth (quintic), ease-in/out/in-out (cubic), exponential, custom curve (four control values), noise-modulated (curve × (1 + a·noise)). The same enum is used for effector strength and for wave envelopes.

## 4. Effectors

Effector = (input attribute set, field, operation): transform/position offset (vector field × strength added to position), scale (scalar field → scale multiply or set), rotation (vector field → axis-angle or scalar → yaw), velocity, colour (colour field mix), density (scalar field multiply), generic attribute (write a scalar/vector into a named attribute). Blend modes: add, multiply, replace, min, max, mix(weight).

## 5. GPU evaluation

Fields become a uniform array of fixed 96-byte records (kind, transform, strength, falloff, kind params) plus a WGSL library `fields.wgsl` with `fieldScalar(i, p, t)`, `fieldVector(i, p, t)`, `fieldColor(i, p, t)` implemented with the same maths as the CPU (shared noise `fbm3`, curl by finite differences of a potential). The same include serves the procedural vertex shader (field displacement, field-driven emission), the particle simulate pass (field forces) and the point-processing compute pass (effectors at 1M points), so results agree across CPU and GPU within float rounding.

## 6. Waves

A travelling wave field: `amplitude · envelope(d − c·t) · shape(k·(d − c·t))` where `d` is distance from origin along the wave's geometry (plane, radial in a plane, spherical, cylindrical), `envelope` a falloff over the wave's width, and `shape` sine/pulse/triangle. Driving `amplitude` from `audio.onset` through a peak-hold route makes each hit a wave that propagates outward — the "onset → wave → bend → emission → particles" chain in the brief is one wave field consumed by an effector, a vertex deformer, a material input and a particle force.
