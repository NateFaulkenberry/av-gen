# ADR-1142: An SDF object can draw a density iso-surface sharpened toward its own tree

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, step 3)
**Date:** 2026-10-05
**Resolves:** `docs/prototypes/astral-forge/04-architecture.md` "Production path" item 2's consumer ("a new
SDF-renderer mode, iso of a density volume ⊕ an SDF tree × S"), the prototype's recommended approach E.
**Implemented by:** `shaders/sdf_raymarch.wgsl` (`sdfDensityAt`, `sdfDensitySurfaceField`, the
`@@SDF_SURFACE_BEGIN/END@@` block), `src/rendering/sdf_renderer.{hpp,cpp}` (the density-mode pipeline
variants, group-1 bindings 5 and 6, `SdfObjectUniforms::density0..2`), `src/scene/sdf_object.{hpp,cpp}`
(`SdfDensitySource`, JSON, `density/iso` and `density/sharpness`), `src/scene/composition.cpp` (the load-time
refusal and the nesting prefix), `src/rendering/scene_renderer.cpp` (hands the particle renderer to the SDF
update), `examples/astral-forge/latent-entity.{scene.json,json}`
**Tests:** `tests/unit/test_particle_latent.cpp` (round trip, refusals by name, the load-time reference
refusal, parameters); `tests/rendering/test_particle_latent_gpu.cpp` ("density mode: a surface where the matter
is, none where it is not, none without matter", "density mode: sharpening pulls the surface onto the tree's zero
set"); `[gpu][sdf][density]`.

## Context

The prototype benchmarked five representations (`04-architecture.md`). D marched the latent SDF directly.
It drew the whole face with no matter present ("a 3-D model with particles around it") and cost 19-29 ms.
E marched the iso-surface of the particle density, pulled toward the latent's zero set only where matter
already was. It was the only approach in which the form was "both precise and emergent", and the
sharpening was free because the latent was evaluated only where the density exceeded 12% of the iso level.
Production's SDF renderer could only march its own tree.

## Decision

**An SDF object may name a particle system's density volume (ADR-1141):**

```json
"density": {"particles": "matter", "iso": 1.5, "sharpness": 0.85}
```

`iso` (> 0, default 1) is the density level the surface sits at. `sharpness` (0..1, default 0) is how far it
is pulled onto the object's own tree. The field the march, the normal, the occlusion and the soft shadow read
is the prototype's `surface.wgsl` `fieldAt`:

```
dRho = (iso - rho) * cell * 1.6
f    = sharpness > 0 && rho > 0.12 iso ? mix(dRho, max(sdf, (0.38 iso - rho) * cell * 3), sharpness) : dRho
```

- `rho` is the volume's r channel at the world point. It is sampled with a linear sampler, and is 0 outside
  the volume so the clamped edge texels cannot smear out to the march's bounds.
- `cell` is one voxel in the object's local units: the mean world cell over the cube root of the model's
  determinant.
- `sdf` is the object's own tree, interpreted or compiled (ADR-1003). It is evaluated only where matter
  already is.

With no matter there is no surface, whatever the tree says.

**One field function, outside the compiler's markers.** Every march of the pass reads one function,
`sdfSurfaceField`, which lies outside the `@@SDF_FIELD_BEGIN/END@@` block a compiled tree replaces. The lit
pass and the depth prepass still reach bit-identical `t` (ADR-1002's speckle), because there is still one
march. The density field calls `sdfField` once: a compiled field is inlined at every call site, and two call
sites would double its code.

**A pipeline variant, not a branch.** The first version branched inside `sdfSurfaceField` on a uniform
(`density0.w`). It rendered every existing object correctly, but **not byte-identically**:

- `ferrofluid-crown.json` at `--range 2:2` (two compiled trees) differed from the pre-change binary in
  5192 channels, max 2 codes;
- the other two reference renders did not move at all.

The branch changed the Metal compiler's arithmetic for objects that never take it. This is the drift
ADR-388 measured for a uniform passed as a parameter.

So the default `sdfSurfaceField` is `return sdfField(...)` and nothing else, between new
`@@SDF_SURFACE_BEGIN/END@@` markers. A density-mode object draws with a **variant** whose module splices
`return sdfDensitySurfaceField(...)` in there:

- the interpreter's density variant, built once on first use;
- or, for a compiled tree, a compiled variant keyed by the tree **and** the density flag.

Every other object keeps exactly the module it had. Measured after the change, against the pre-change
binary: ferrofluid, stellar-nursery and the particle lab all have **0 differing channels**.

**Bindings.** Group 1 gains binding 5, `texture_3d<f32>` (the volume, or a 1×1×1 zero placeholder), and
binding 6, a filtering sampler. `SdfObjectUniforms` grows from 352 to 400 bytes (of its 512-byte slot) with
`density0` (iso, sharpness, cell, 1 = on), `density1` (the volume's min corner) and `density2` (1 / its
extent). Group 1 is one shared group with dynamic offsets. An object with a volume therefore uses a group that
binds that view, cached per view and looked up when the pass is encoded. The node buffer can be re-created
after the items are collected, and that re-creates every group.

**Resolution.** `SdfRenderer::update` takes the particle renderer, which has already run this frame. It asks
it for the named system's volume. If the volume does not exist, the object is **not drawn**: no matter, no
surface. This is said once in the log.

**Refused at load, by name.** `Composition` refuses a scene in which `density.particles` names no particles
node, or names one with no `density` block:

```
node 'body': density.particles 'ghost' names no particles node in this scene
node 'body': density.particles 'plain' has no density volume (give it a 'density' block)
```

It also refuses a density source on a `renderMode: mesh` object (`SdfObject::validate`). Malformed keys are
refused by name.

**Parameters.** `sdf/<name>/density/iso` and `sdf/<name>/density/sharpness`. They are registered only when
the block is present, and both are uniforms.

**Density is not a distance.** `dRho` is a density difference scaled to about one cell per unit, not a
Lipschitz bound. A density-mode object therefore wants:

- a `stepScale` below 1 (the example uses 0.6);
- a step budget that crosses its bounds in steps of roughly `iso · cell · 1.6` through empty space (the
  example uses 448 over a 4.8-unit box at a 0.025 cell);
- bounds that cover the volume.

The prototype's 24³ max-occupancy grid, which skips empty space in 8³ blocks, is not ported (see the
revisit triggers).

## Consequences

- An object without the block draws with the module it always had: byte-identical, measured above.
- The surface appears where the matter is and nowhere else. The test's tree is two spheres and the matter
  binds to one of them. The other sphere is never drawn at sharpness 0 or at sharpness 1. With no matter,
  nothing is drawn at all.
- At sharpness 1, the iso level no longer moves the surface (row width within 2 px for iso 0.3 and 1.2). At
  sharpness 0 it does: the surface is the tree's own zero set wherever matter is dense enough.
- The first density-mode object costs one synchronous pipeline build. It was measured at 1177 ms in the
  example's offline render, and 104 ms for the second renderer in the same process. A live editor that
  loads such a scene hitches once. A compiled density object compiles on Dawn's workers like any compiled
  tree (ADR-1102) and is drawn by the interpreter's density variant until it is ready.

### Measured cost (M2 Max, 1920×1080, the example's crude mask)

Timed with `avgen_render_tests "[.perf][latent]"` under the GPU lock, p50 of the frames after warm-up. The particle
rows time the particle renderer alone, on a timeline that marks only its two passes. 1M particles start in a
box and are bound at coherence 0.93.

| Arm | Particle compute pass | Density pass | SDF raymarch pass | GPU frame |
|---|---|---|---|---|
| 1M particles, no latent | 1.05 ms | -- | -- | |
| + latent (the crude mask, 7 packed records) | 7.01 ms | -- | -- | |
| + density 128³ | 7.08 ms | 0.59 ms | -- | |
| + density 192³ | 7.08 ms | 0.92 ms | -- | |
| + density 256³ | 7.14 ms | 1.57 ms | -- | |
| the mask tree raymarched directly (448 steps, step 0.9), 1080p | | | not recorded (see below) | 26.5 ms |
| density mode, sharpness 0.85 (448 steps, step 0.6), 1080p | | | 7.86 ms | 24.7 ms |

- **The latent costs about 6 ms per million particles** for this tree: 4 interpreter evaluations per
  bound particle per step. The prototype paid 8.7 ms for 2M with a staggered projection that refreshes every
  third step. The stagger is not ported (ADR-1140).
- **The density volume costs 0.6-1.6 ms**, scaling with cells.
- **Density mode is not more expensive than marching the tree directly**: the whole-frame GPU time is
  1.8 ms lower in the same scene. For the direct march, the frame timeline returned no `sdf` interval in any of
  the 60 frames. That pass's own time is therefore not quoted, and the whole frame is the comparison. A
  frame-filling density surface will cost more than this mid-frame one, as the prototype found
  (`05-tests-and-assessment.md`), and the coarse occupancy grid is the remedy (revisit triggers).

## Rejected alternatives

- **A uniform branch in the shared field function.** It was built, measured and replaced. It is not
  byte-identical for other objects (above).
- **A separate render pass or renderer for density surfaces.** It would duplicate the march, the PBR
  shading, the depth prepass and the shadow entries that `sdf_raymarch.wgsl` already has, and the
  duplicate would drift.
- **Marching the tree directly and masking it by density.** That is approach D with a cut-out. It draws the
  tree's surface wherever any matter is near, instead of the matter's own surface sharpened toward the tree.
- **Binding the density in group 0 (the frame).** One volume per frame would be the limit, and every lit
  pipeline in the engine would gain a binding it never reads.

## Revisit triggers

- A density-mode object fills the frame and its raymarch exceeds a few ms. Port the prototype's coarse
  max-occupancy grid: a second, tiny 3-D texture written by the resolve pass, so the march skips empty
  blocks.
- A scene wants the density surface without any tree (approach C). Today that is `sharpness: 0` with any
  tree, which already evaluates no tree at all.
- The first-use variant build hitches a live show. Build the density variant asynchronously, as ADR-1102 does
  for compiled trees.
