# ADR-1893: A world scale for the canyon

- Status: Proposed (Art Pass 2, M1b), scene/strange-attractor.
- Code: `AttractorGen::worldScale` and `scaleWorld` (`src/scene/attractor_world.*`); the scene side is
  `examples/strange-attractor/canyon_scale.py`.
- Tests: `[adr1893]`.

## Context

With Sasquatches in it, the owner found the canyon far bigger than they had pictured. The river was about 150 m wide,
the inner gorge 70 m deep and the outer walls 150 m. The character action belongs on the benches beside the river, and
a 2.3 m figure there read as a speck. The canyon still has to feel grand.

## Decision

The world is scaled down uniformly. Characters and props keep their real size.

- **`attractorWorld.worldScale`** (0.1..2, absent = 1). The generator works at scale 1 and the finished world is scaled:
  - the height field and the strata offsets;
  - the masses, the side canyons and the network distances;
  - the strata band and fold;
  - the composed camera and the flight;
  - the terrain mesh and the river.

  The shape is therefore identical. The flight keeps its timing, so its speeds scale with it. The approach (ADR-1881)
  is authored in world units like the rest of the scene and is brought back to generator units for generation. The
  strata coordinate (in bands) and the baked visibility are scale-free and stay. A scale of 1 is Pass 1's hash and
  world exactly.
- **Everything else in the scene scales with it** (`canyon_scale.rescale`, idempotent by ratio):
  - the camera, the fog density, the shadow range and the cirrus;
  - the water's ripple frequency, flow, depth fades and SSR distance;
  - the terrain program's world-position frequencies and depth fades;
  - the scan's distances, and the portal's corruption (aperture, blocks, slip, glyph cells, field, light as d²);
  - the project's corruption slip route.

  Portal 1 stays in its bank wall by the river, scaled.
- **The scale chosen is 0.4.** The river is about 60 m wide, the gorge about 27 m deep, the benches about 2 m over the
  water by the camp and the outer walls 40-60 m.

## Consequences

- The opening is the same picture at a smaller size. The measured comparison against Pass 1 is in `36-pass2-m1b.md`.
- New canyon positions are authored in real metres through `canyon()` in the builders.
- A Moon crossing carries the camera's scaled speed onto the Moon (unscaled): the Moon's route sets its own pace from
  the first knot.
