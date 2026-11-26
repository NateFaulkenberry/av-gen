# ADR-2246: a traced surface is shaded in the raster's order, programs included, at the frame's render clock

- Status: Accepted (2026-11-26), eng/hybrid-rt (Hybrid RT H2; audit defects D4, D21)
- Code: `pathtrace/surface.{hpp,cpp}` (new), `pathtrace/snapshot.{hpp,cpp}`, `pathtrace/path_tracer.cpp`,
  `pathtrace/trace_job.cpp`, `app/trace_sequence.cpp`
- Tests: `tests/unit/test_pathtrace_materials.cpp` (`[adr2246]`)

## Context

The tracer shaded a hit from the material's factors and textures alone:

- **Programs ignored.** Material programs never ran. In Neon Dreams there are 35 programs, run by 870 materials, and
  they decide which windows are lit, colour the gardens and draw the moon.
- **Instance lanes ignored.** The colour and emissive lanes of procedural instances were dropped. These are the hue
  and emissive variation that `procedural.wgsl` multiplies in.
- **Emission gain misapplied.** The object's emission gain was folded into the factor, where a program that writes
  emission discards it.

## Decision

1. **One shading function, `shadeSurfaceMaterial`, in the order `pbr_shade.wgsl` `shadeSurface` uses.**
   1. The factors.
   2. The program over the factors, when its gate admits the instance at the camera distance. The CPU twin
      `scene::evaluateMaterialProgram` runs it; that function already has a GPU parity test.
   3. The base colour is multiplied by the instance colour lane, then by the textures.
   4. Emission is the program's (or the factor's) value × the instance emissive lane × the emissive texture × the
      intensity. Then come the instance's variation, as hue and gain (ADR-904), and the object's lane (ADR-903/905).
   - The helpers are the engine's existing CPU twins: `color::emissionVariationOf`, `color::livingChromaTurns` and
     `color::hueShift`.
   - Nothing here decides what a material means.
2. **The program context is the raster's.**
   - World and local position. Local is bind space for an entity and the raw source position for a procedural.
   - The facing normal, uv, object and material ids.
   - The instance's random, colour, emissive, index and id.
   - The camera's view direction and distance.
   - Time is the snapshot's `renderTime`. Both trace paths pass the `FrameTime::renderTime` they updated the engine
     with. That is the value `RenderJob` hands the renderer with the same update (`render_job.cpp` `engine_->update(time)`
     then `renderer_->render(..., time, ...)`). The two clocks never differ in a job path, so the tracer uses that one
     source.
3. **Inputs with no ray-traced value are named.** Screen-space geometry, ambient occlusion, audio, tile glow, the
   key light or sky, field ops and texture ops get the neutral value. Each one a frame's programs use is reported by
   name.

## Consequences

Neon Dreams' windows, signs, gardens and moon now come from their programs. On depth-matched pixels, lit/unlit
agreement with the raster's emission AOV went from 94.4–95.7% to 95.8–97.9% across the four audit views. At the
moon view it went from 85.4% to 97.9%. Most of the remainder at t = 20 is the Great Lantern's alpha-blended glass
and thin lattice.
