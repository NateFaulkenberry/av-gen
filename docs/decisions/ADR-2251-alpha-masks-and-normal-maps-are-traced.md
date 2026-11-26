# ADR-2251: alpha masks and normal maps are traced; alpha blend is reported

- Status: Accepted (2026-11-26), eng/hybrid-rt (Hybrid RT H2; audit defect D9)
- Code: `pathtrace/path_tracer.cpp` (`cutOut`, `clear`, the intersection loop), `pathtrace/surface.cpp` (the normal),
  `pathtrace/bsdf.hpp` (`SurfaceMaterial::normal`), `pathtrace/snapshot.{hpp,cpp}` (`hasAlphaMask`, report rows)
- Tests: `tests/unit/test_pathtrace_materials.cpp` (`[adr2251]`)

## Decision

1. **Alpha mask.**
   - **Where it is decided.** In the integrator, not in Embree filter callbacks. Those would have to hold a per-frame
     snapshot inside child scenes that ADR-583 reuses across frames.
   - **What every ray does.** It passes through a masked surface whose opacity is below the cutoff, and goes on. The
     opacity is the factor × the program × the texture alpha, which is what the raster discards on.
   - **Shadow rays** walk through cut texels hit by hit, but only in a scene that has a masked material. Every other
     scene keeps Embree's occlusion query.
   - **Not applied:** the raster's distance fade of the cutoff.
2. **Normal maps and the program's normal output.**
   - The program's tangent-space normal comes first. The normal map is then reoriented over it, with `normalScale`
     on xy, through `scene::reorientNormal`.
   - The result is applied in the raster's cotangent frame. The tracer builds that frame from the triangle's own
     edge and uv gradients, which describe the same gradient the raster takes from screen derivatives.
   - Like the raster, it falls back to a position-gradient frame when there is no uv gradient.
   - The perturbed normal shades only if it is in the geometric hemisphere.
3. **Alpha blend** stays opaque and is reported (Neon Dreams: 637 materials, including glass). Stochastic
   transparency is the later option.
