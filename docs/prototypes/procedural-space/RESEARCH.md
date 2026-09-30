# Procedural Space POC: research report (Phase 2)

*Engineering agent, 2026-09-29. Short by intent (§4: "inform the implementation, not an academic
exercise"). It builds on the repository's own earlier study,
`docs/research/sdf-and-implicit-geometry.md` (ADR-027), which is not repeated here.*

## Findings

### What matters most for impossible architecture

1. **Domain operations, not primitives, make the space.** Quilez's catalogue is exact for primitives,
   rounding, onion, uniform scale (`sdf(p/s)*s`), symmetric repetition and mirroring of shapes that do
   not cross the mirror plane. Twist, bend, displacement, repetition of **asymmetric** content and
   mirroring across content are **bounds only**: the field's Lipschitz constant exceeds 1, and a
   sphere tracer oversteps unless it is relaxed. [distfunctions]
2. **Repetition is the enormous-environment lever, and the caveat that matters is neighbouring
   cells.** Naive `p - s*round(p/s)` looks only in the current cell. If an instance reaches past its
   cell (a wall thicker than half the spacing, an asymmetric doorway), distances near the boundary
   are wrong and rays punch through or stall. The fixes are to check the 2 (1D) to 8 (3D) neighbouring
   cells, which costs up to 8x, or to **mirrored repetition**: flip every other cell, which keeps the
   single evaluation and holds for any content. Clamping the cell id (`clamp(round(p/s), -n, n)`)
   gives finite repetition that stays correct. The engine's `repeat` already does this with `count`.
   [sdfrepetition]
3. **Folding is repetition's generalisation.** `abs(p)` is a mirror (already `mirror`). A plane fold
   `p -= 2*min(0, dot(p,n)-o)*n` reflects the half-space behind any plane. Iterating fold, scale and
   offset (the "KIFS" scheme behind Menger-like and cathedral-like fractals) gives **recursive
   architecture** in one loop. Each iteration divides the distance by the scale, so the result stays a
   true distance bound: folds are 1-Lipschitz, and uniform scale is exact. Rotating between iterations
   gives the "impossible" skew without a Menger look.
4. **Structural state changes need interpolation between fields.** A convex mix `mix(dA, dB, t)` of two
   SDFs is a valid bound (it is 1-Lipschitz). So one float can morph a corridor into a chamber
   continuously, and a quantised float switches it discretely. This is the brief's "the drop changes
   the rules" as one parameter.

### Sphere tracing

- **Bound everything:** maximum steps, maximum distance and a hit epsilon. For a perspective camera the
  epsilon should scale with distance (`d < eps * t`), so the threshold stays about a pixel wide at any
  depth. The engine already does this. [raymarchingdf]
- **Non-exact fields:** relax the step with `t += d * w`, where `w < 1`. That is the existing
  `stepScale`. It costs steps in proportion to `1/w`; `w ≈ 0.6-0.8` is typical for twist and bend at
  moderate amounts.
- **Over-relaxation** (Keinert et al. 2014, "Enhanced Sphere Tracing") uses `w ∈ [1, 2)` with a
  fallback when consecutive unbounding spheres stop overlapping. It is safe for exact fields and saves
  roughly 10-30 % of steps. It is only worth doing if the step count is measured to be the bottleneck.
- **Normals:** the four-tap tetrahedron difference (already `sdfNormal`) costs four evaluations at the
  hit only.
- **AO:** 5 samples along the normal, `ao = 1 - k * Σ (h_i - d(p + n*h_i)) / 2^i`. That is five
  evaluations per hit pixel, cheap, and much better on SDFs than screen-space AO because it sees
  geometry off screen.
- **Soft shadows:** march toward the light from the hit, `res = min(res, k*h/t)` (Aaltonen's variant
  removes banding). It is a second full march per pixel, typically 24-64 steps, and the most
  expensive of the look terms. The engine already renders SDFs into the cascaded shadow maps, so the
  march is optional.
- **Edges:** the difference between the tetrahedron samples at a wider offset and the surface (a
  discrete Laplacian) is near zero on flats and large on creases. That gives "emissive edges" from four
  extra evaluations at the hit, with no mesh edges needed.
- **Fog:** exponential distance fog on the march length. The engine's fog in `shadeSurface` (and the
  volumetric pass) already applies to SDF hits.
- **Implementation shape in the references:** the WebGPU Menger example uses a full-screen triangle,
  128 steps with FAR = 30, a tetrahedron normal, 5-sample AO and a 40-step penumbra shadow, all in one
  fragment shader. That is the same structure as the engine's pass, minus the scene integration.
  [N0Xl0US]

### Audio to rules (§19-21)

Continuous bands go through smoothing (attack and decay on the route chain) into **rule parameters**:
`repeat/size`, `twist/amount`, `recurse/scale`, `bend/amount`, fold offsets. Discrete events (beat,
bar, section, drop from `music.*`) go into **structure**: `count`, `morph/amount` steps, `enabled`
toggles and scene-state changes. The rule layer is what separates "the space re-rules itself" from
"everything bounces". Rule parameters move slowly; structure moves on events.

## Candidate approaches

| | A. SDF ray marching | B. CPU procedural meshes (ADR-023) | C. GPU-generated meshes | D. Compute meshing (marching cubes, surface nets on GPU) | E. Hybrid SDF + raster | F. Compiled SDF (tree to WGSL) |
|---|---|---|---|---|---|---|
| visual flexibility | highest: booleans, folds, recursion, infinite repetition | instances of fixed shapes; no booleans | as B, animated | whatever the SDF says, at grid resolution | A's plus assets | as A |
| topology changes | free, per frame | rebuild per change | limited | a re-mesh per change | free on the SDF side | free; a structural change recompiles (tens of ms) |
| performance | per pixel × steps × tree cost; interpreter overhead | cheap to draw; rebuilds cost CPU | cheap | a heavy volume per frame; resolution-bound | as A plus raster | per pixel × steps; roughly 3-10x cheaper per step than the interpreter |
| complexity here | **already built** (ADR-027); vocabulary to add | already built | new pipeline | new, large | **already built** (depth composition) | medium: codegen, pipeline cache, a parity test |
| AV Gen integration | composition node, params, routes, states, offline | same | new | new | same as A | same as A |
| audio reactivity | any rule parameter, live, no rebuild | deformers and params; structural changes rebuild | per vertex | re-mesh latency | as A | as A; structure changes stall a frame |
| offline render | the same pass, deterministic | yes | yes | yes | yes | yes |
| future extensibility | node vocabulary; promotion to F | good for instanced dressing | moderate | exports meshes (a real use: SDF to asset) | assets inside impossible space | the production form of A |

Short verdicts:

- **B** is how the Infinite Temple and Cathedral examples were built. They show its ceiling: they
  cannot fold, subtract a doorway or recurse.
- **C and D** solve a problem the POC does not have: meshes. §31 forbids extraction first, and the
  engine's CPU surface nets already exist for the "SDF to asset" case.
- **E** is not an alternative. The existing pass writes `frag_depth`, so it is A plus whatever
  meshes the scene already has.
- **F** (found here) is A's own promotion path: the same data and parameters with no interpreter.

## Recommendation

**Choose A on the existing ADR-027 path, with E for free, and keep F as the measured fallback.** The
hypothesis in §11 holds, for a reason the brief could not know: the engine already built A, and its
cost is known in shape. What is not known is whether the **interpreter** is fast enough for a
full-screen architectural tree. Phase 3 measures that first, at render scales 1.0, 0.75, 0.5 and 0.33.
If the interpreter fails, F replaces the inner loop without touching the scene data, the parameters or
the pass structure.

What to add, in order of value to the art agent:

1. `morph` (structure states as one float)
2. `recurse` (iterated fold, scale, rotate and offset: nested architecture)
3. plane `fold`
4. `count` as a parameter
5. named nodes
6. SDF AO and edge emission
7. step statistics
8. maximum distance

Soft shadows come last, and only if the frame has room.

## Sources

- [distfunctions] Quilez, "Distance functions": https://iquilezles.org/articles/distfunctions/
- [raymarchingdf] Quilez, "Ray marching distance fields": https://iquilezles.org/articles/raymarchingdf/
- [sdfrepetition] Quilez, "Domain repetition": https://iquilezles.org/articles/sdfrepetition/
- [rmshadows] Quilez, "Soft shadows in raymarched SDFs": https://iquilezles.org/articles/rmshadows/
- Keinert et al., "Enhanced Sphere Tracing", STAG 2014: https://diglib.eg.org/items/8ea5fa60-fe2f-4fef-8fd0-3783cb3200f0
- [N0Xl0US] WebGPU raymarch examples: https://github.com/N0Xl0US/raymarch_examples
- Background only, not dependencies (§30): libfive (functional representation is the same idea as
  ADR-027's data tree; its value would be interval-arithmetic culling, not needed at this scale);
  OpenVDB/NanoVDB (sparse volumes, for baked or simulated SDFs, not procedural ones).
