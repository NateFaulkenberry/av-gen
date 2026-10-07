# ADR-1181: A fiber follows the streamlines of its fields

**Status:** Accepted (proto/chorus-field)
**Date:** 2026-10-06
**Brief:** `docs/prototypes/chorus-field/00-brief.md` (§6, the spatial field system; §17, performance).
**Implemented by:**
- **Scene side:** `scene::DeformerKind::Streamline` and `scene::fiberCentreLine` (the CPU reference), in
  `src/scene/procedural.{hpp,cpp}`.
- **Renderer:** the strand pass in `src/rendering/procedural_renderer.cpp`: `FiberStrandUniforms`,
  `ensureStrandGroup`, and the `procedural-fiber-strands` compute pass.
- **Shaders:** `shaders/fiber_strands.wgsl`, and `fiberStrandPoint` and `fiberPoint` in
  `shaders/procedural.wgsl`.
- **Editor:** the World panel's deformer rows, in `src/ui/ui_logic.hpp` and `src/ui/world_panel.cpp`.

**Tests:**
- `tests/unit/test_fiber_field.cpp`: a streamline keeps the fiber's arc length; it follows its field; amount 0
  is straight (the control); stiffness leaves the root along the fiber's own axis; and it is refused on a
  non-fiber source and without a field.
- `tests/rendering/test_fiber_field_gpu.cpp`: every point of the CPU reference is lit on the GPU at three
  steerings, and the straight fiber's tip is not (the control). A missing field leaves the fiber straight.
- `tests/unit/test_deformer_panel.cpp`: the panel's rows for the new kind.

## Context

The Echo Field's structure comes from fields sampled at each element's position: neighbours agree, so rings
and fans appear. A filament can carry far more structure than a reed, because it lies along the field rather
than sampling it once. A million filaments lying along a few cheap vector fields draw the field's streamlines,
which is line-integral convolution made of geometry. That is where streams, sheets, wakes, voids, rolls and,
in the brief's test, faces come from.

The existing Field deformer displaces a vertex by the field's value. That shifts a fiber; it cannot make one
follow a flow.

## Decision

1. **A `streamline` deformer** names a vector field and steers a Fiber source (ADR-1180) along it:
   ```json
   {"kind": "streamline", "space": "world", "field": "flow", "amount": 6.0, "falloff": 0.0}
   ```
   From the instance's root `r_0` and axis `d_0` (its rotation of +Y, through the object matrix), with
   `P(r, a) = Σ_i T_i(v_i(r) · amount_i · ramp_i(a))`:
   ```
   h = turn(d_k, P(r_k, k·ds), ds/2),   m = r_k + h · ds/2
   d_{k+1} = turn(d_k, P(m, (k+½)·ds), ds),   r_{k+1} = r_k + d_{k+1} · ds,   turn(d, p, h) = normalize(d + p·h)
   ```
   - Each step is a **midpoint (RK2) step.** Explicit Euler, the first cut, left visible kinks and drifted
     outward on tight turns at 1-2 m segments.
   - `ds` is the segment length times the instance's length scale. Fibers have up to 64 segments.
   - `ramp_i(a) = clamp(a / falloff_i, 0, 1)`, or 1 when falloff is 0. `falloff` is a **stiffness**: the fiber
     leaves its root along its own axis.
   - `T_i(p) = p · max(|p| − tension_i, 0) / |p|`. `tension` is a **dead zone**: a weak field leaves the wire
     straight and only a strong one bends it. That gives straight runs and sharp bends, like wire under load,
     not hair.
   - **Amount** is steering, in 1/m per unit of field. 0 is a straight fiber; large values follow the
     streamline exactly.
   - **Arc length is preserved** whatever the field does.
   - **Every Streamline deformer of a stack adds its own pull.** A layer is steered by up to eight separately
     weighted forces, each with routable `procedural/<n>/deform/<k>/{amount,falloff,tension}`. This also lifts
     the GPU's limit of four children in one compound field.
   - `space` is ignored; the field is always sampled in world space. A Streamline on any non-fiber source is
     refused at load.
2. **The strand pass.** After the effector pass, one compute thread per record integrates the centre line once
   and writes its `segments + 1` points into the object's strand buffer (16 B each).
   - The vertex stage of every pass reads it: the camera pass, the prepass and the shadows. The tangent is the
     central difference of the neighbouring points.
   - A coarser LOD mesh reads the nearest buffer point by arc fraction.
   - The buffer is bound at group 1 binding 7, which is ADR-056's Tier 1 bend array. A fiber layer is never a
     simulated plant layer, so the layout and the per-stage storage-buffer budget are unchanged.
   - The first Streamline deformer's spare lanes say whether the buffer is bound (`params.y` = segments,
     `params.z` = 1). If it is not, the vertex stage integrates the line itself. It is the same rule, so the
     picture is the same, but it costs O(segments²) field samples per fiber per pass.
3. **Determinism.** The line is a pure function of (record, fields, t). Fields include the ADR-1116 audio
   kinds, read with the record's own `random.w` as its element. So offline renders, seeks and live playback
   agree exactly as they do for effectors. `scene::fiberCentreLine` is the CPU reference, operation for
   operation.

## Consequences

**Measured** (M2 Max, 1080p Ultra): the strand pass costs 0.98 ms for the mask (5.3k fibers × 32 segments, 4 pulls, Euler), and 2.5 ms for the god (5k × 24 segments, 5 pulls, RK2).
Without it, a 192k × 32 scene queued enough vertex work that Dawn returned an all-black frame with "GPU errors:
0". That is the known long-backlog defect, `docs/research/gpu-world-productionization.md`, Risks item 1. With
it, the same scene renders. The strand buffer costs records × (segments + 1) × 16 B: 101 MB for 192k fibers
of 32 segments.

**Fields are the only vocabulary.** Every shape in the Chorus Field comes from the engine's existing field
kinds:
- vortex, inward spiral, attractor, direction and curl noise;
- the onset × radial-vector compound used for the kick shockwave;
- scalar fields that thin the fibers through Scale effectors.

**The midpoint step costs two field samples a step.** That is 2.5 ms for 5k fibers × 24 segments with five
pulls (Phase 6 in `docs/prototypes/chorus-field/README.md`). Dispatching over culled-visible records only is
the next saving. An inward *spiral* field, not integration drift, is what makes a coil.

## Rejected alternatives

- **Integrating per vertex only** (the first cut). It needs no buffer, but every vertex of every pass
  re-integrates its prefix. That is fine for a few thousand fibers and fatal at a few hundred thousand.
- **A curve control-point fit** (4-6 control points per fiber, evaluated as a spline in the vertex stage). It
  is cheaper in memory, but it smooths away exactly the tight turns that make wakes and eye sockets read.
- **Particle advection with trails.** That is state, history and seeking, for a picture that is a pure function
  of the field.
