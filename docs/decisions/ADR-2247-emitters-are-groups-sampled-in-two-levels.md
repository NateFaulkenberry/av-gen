# ADR-2247: emitters are groups sampled in two levels, instanced ones included, lit by the same shading as a hit

- Status: Accepted (2026-11-26), eng/hybrid-rt (Hybrid RT H2; audit defects D7, D8)
- Code: `pathtrace/snapshot.{hpp,cpp}` (`EmitterGroup`, `EmitterTriangle`), `pathtrace/lights.{hpp,cpp}`
  (`sampleEmissive`, `emissivePdf`), `pathtrace/path_tracer.cpp`
- Tests: `tests/unit/test_pathtrace_materials.cpp` and `test_pathtrace_mis.cpp` (`[adr2247]`)

## Context

Two defects in next-event estimation:

- **Instanced emitters were invisible to it.** Only mesh triangles were in the emitter list. Every procedural window
  and sign could therefore be found by BSDF rays alone.
- **The emitted radiance it used was wrong for some emitters.** It took the untextured factor, while a BSDF hit used
  the textured value. The two strategies then estimated different integrals.

## Decision

1. **Groups.** An emitter group is one emitting mesh, or one copy of an emitting procedural source.
   - A surface can emit when its factor is non-zero or its program writes emission.
   - A group is chosen by its weight: world area, approximate for a scaled copy.
   - A triangle within the group is chosen by area. Meshes use world area. Instanced sources use object-space area,
     shared by every copy.
   - A point is chosen uniformly on that triangle.
2. **The density is exact whatever the scales.**
   - Mesh: `p_A = P(group) / A_group`.
   - Instance: `p_A = P(group) × A_obj(tri) / A_obj(group) / A_world(tri)`.
   - Both are computed from the hit's group index, which resolves through per-mesh and per-source tables. No
     per-triangle map is needed.
3. **The radiance at a sample is `shadeSurfaceMaterial(...).emission`.** Textures, programs and instance lanes
   therefore light the scene exactly as a hit shows them. A sample that lands on a dark part costs no shadow ray.

## Consequences

- Neon Dreams has 3,940 emitting surfaces, 2,022 of them instanced copies.
- Sampled pdf equals `emissivePdf` for non-uniformly scaled copies (to 1e-4).
- Light-only, BSDF-only and MIS agree within 4% on a floor lit only by programmed, instanced emitters.
