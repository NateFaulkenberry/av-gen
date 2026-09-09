# SDFs and implicit geometry

Status: research (2026-09-09). Decision: ADR-027.

## 1. Primitives, operators and displacement
Inigo Quilez's distance-function catalogue is the de-facto reference: exact/bound distances for sphere, box, rounded box, cylinder, capsule, torus, plane, cone; boolean union/intersection/difference and their smooth variants (`smin` polynomial), domain operations (translate/rotate/scale, twist, bend, repetition, polar repetition, mirror) and displacement (`d + f(p)`, which breaks the Lipschitz bound and needs smaller steps). (https://iquilezles.org/articles/distfunctions/ ; …/articles/smin/ ; …/articles/raymarchingdf/ — accessed 2026-09-09.)

## 2. Rendering options
- **Sphere tracing** (Hart 1996): march by the distance value; needs a bounded derivative; add a relaxation factor and a max step count; normals by central differences or tetrahedron trick. Writes depth so it composes with rasterised geometry (research/rendering-techniques.md E.2).
- **Meshing**: Marching Cubes (Lorensen & Cline 1987) — simple, lookup tables, non-manifold-safe variants; **Surface Nets / Naive Surface Nets** (Gibson 1998; Lysenko's comparison https://0fps.net/2012/07/12/smooth-voxel-terrain-part-2/ — accessed 2026-09-09) — one vertex per cell, quads across edges, smoother and far simpler than dual contouring; **Dual Contouring** (Ju et al. 2002) — sharp features but needs Hermite data and QEF solves.
- Choice: surface nets on the CPU for authoring/meshed SDFs (deterministic, simple, cached by hash), sphere tracing on the GPU for the direct path (huge or highly deformable forms); GPU meshing later if needed.

## 3. Determinism and cost
- The SDF tree is data (nodes with kind, params, children); evaluated identically in C++ and WGSL (same functions, same order). Displacement uses the shared `fbm3`.
- Cost: raymarch is C2–C4 at 4K (rendering-techniques.md); render at half resolution with depth-aware upsample when needed; bound each SDF object with its AABB to limit the marched pixels.

## 4. How Notch does it
Procedural (SDF) systems take primitives, meshes and particles as inputs, combine with CSG and displacement, and render by meshing or directly. (https://manual.notch.one/2026.1/en/docs/nodes/procedurals/ — accessed 2026-09-09.)
