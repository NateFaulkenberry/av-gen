# ADR-1180: A procedural source can be a fiber

**Status:** Accepted (proto/chorus-field)
**Date:** 2026-10-06
**Brief:** `docs/prototypes/chorus-field/00-brief.md` (§4, the fundamental primitive).
**Implemented by:**
- `scene::PrimitiveKind::Fiber`, the `SourceSpec::fiber*` fields, `makeFiberStrip`, the LOD rule and the JSON and
  parameters in `src/scene/procedural.{hpp,cpp}`;
- `fieldInfo.z = 2` and the two-sided draw in `src/rendering/procedural_renderer.cpp`;
- `fiberVertex` in `shaders/procedural.wgsl`.

**Tests:**
- `tests/unit/test_fiber_field.cpp` (`[fiber]`): the strip's lanes, the LOD levels, the refusals and the JSON
  round trip.
- `tests/rendering/test_fiber_field_gpu.cpp` (`[gpu][fiber]`): the centre line on the GPU, and the pixel floor.

## Context

The Echo Field's reeds are 5-sided cylinders drawn through the full procedural vertex stage. That stage runs the
whole deformer chain four times per vertex: three times for a finite-difference normal and once for last
frame's position. A reed is one segment, so that cost is affordable there.

A filament is different. It is hair-thin, many segments long, almost always narrower than a pixel, and seen from
every side. As a cylinder it would be tens of triangles per segment, and all of them would be sub-pixel. As a
raw line it would have no width, no shading and no anti-aliasing.

## Decision

A procedural object's source may be a **fiber**:

```json
"source": {"kind": "fiber", "fiberLength": 36.0, "fiberWidth": 0.008, "fiberTaper": 0.3,
           "fiberSegments": 32, "fiberMinPixels": 0.7}
```

**The mesh** is a strip of `fiberSegments` quads (2 triangles each) from the root at the origin along +Y:
- **Positions:** vertex pair k sits at arc length `s_k = length * k / segments`, at `x = ±halfWidth_k`, where
  the half-width tapers linearly to `fiberTaper` of the root's.
- **The normal lane** carries what the vertex stage needs instead of a normal: (segment length, min pixels,
  root half-width).
- **uv:** (side, k / segments).

**The vertex stage** (`fiberVertex`, chosen by `fieldInfo.z == 2`, uniform per draw) builds the fiber itself:
1. **Centre line.** It comes from the strand buffer when a Streamline deformer is bound (ADR-1181). Otherwise
   it is straight along the instance's rotated +Y. World-space deformers then act on it as on any procedural.
   The instance scale's y multiplies the length, and its x the width.
2. **Facing.** The strip turns to face the camera: across = `normalize(cross(tangent, view))`.
3. **Pixel floor.** The strip is held at no less than `fiberMinPixels` pixels across, measured at 1080 lines,
   so a supersampled render and the live viewport agree. The coverage it had to add is paid for by darkening
   the instance's colour and emission multipliers. A fiber field is drawn on black, so a fiber covering a third
   of its pixel gives a third of its light. A far filament fades instead of breaking into dashes. This is
   Persson's "phone-wire AA".
4. **Shading.** The fiber is shaded as a round thread:
   - **Edge normals.** They turn ±70° across the strip from the view, so the interpolated middle faces the
     camera and the rims catch grazing light.
   - **Kajiya-Kay normal.** When the fiber is narrower than ~4 pixels, the normal blends towards the cylinder
     normal that best reflects light 0 to the viewer: the half vector with its component along the tangent
     removed.

   One pixel cannot sample a thread's cross-section, and without this a field of thin fibers shades as glitter.
   With it, a highlight runs continuously along each filament, and aligned bundles light up together as one
   anisotropic band. It is the shared PBR in `pbr_shade.wgsl`: thin film, IBL, every light and the material all
   apply. There is no new shading model and no new fragment entry.

**LOD.** Every level of a fiber is the same fiber with fewer segments (1/2, 1/4 and 1/8, at least 1). A fiber is
never an impostor quad (`lodLevelIsImpostor`). The cull radius is the fiber's length about its root, which is
the only bound that holds whatever a streamline does.

**Reach.**
- **Scene JSON:** the keys are written only for a fiber, so every older scene round-trips byte-identically.
- **Parameters:** `source/kind` (10 = fiber) and `procedural/<n>/source/fiber{Length,Width,Taper,Segments,MinPixels}`,
  registered for a fiber only. They are structural: the strip is rebuilt.
- **Editor:** the Parameters panel, and the World panel's deformer stack for the Streamline that bends it.

## Consequences

- A fiber is only as wide as a pixel and only as long as its segments allow. Highly curved streamlines show
  polyline kinks at 1-2 m segments; more segments or shorter fibers remove them.
- The Kajiya-Kay normal serves light 0 only. Other lights shade the fiber through the edge normals, so a rim
  light reads weakly on sub-pixel fibers. Colour comes best from light 0, the IBL and thin film.
- The coverage darkening is exact on black and approximate over other fibers: a fading fiber in front of a
  bright bundle darkens it slightly instead of being transparent. It is opaque geometry, and the depth
  prepass, the shadows and the identifier target all work unchanged.
- Cost is fragment-bound. Long overlapping one-pixel strips cost quad overdraw. Measured in
  `docs/prototypes/chorus-field/README.md` (Phase 6).

## Rejected alternatives

- **A tube (`PrimitiveKind::Tube`) per fiber.** It costs 10-24 sides per segment of sub-pixel triangles and has
  no pixel floor.
- **The ribbon renderer (Effect Library Wave 1).** It builds strips on the CPU each frame and blends them
  unsorted. A million CPU-built strips is the problem the GPU path exists to avoid, and blended fibers cannot
  occlude each other.
- **A new "fiber renderer".** That is the Astral Forge failure the brief names (§20). The fiber is a source of
  the existing procedural path, so it inherits distributions, generators, effectors, fields, cull, LOD,
  shadows, materials, parameters and the inspector.
