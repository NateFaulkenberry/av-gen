# ADR-1873: Corruption: a world-space data mosh on the rock around one focus

- Status: Proposed (Gate A, dimensional portals), scene/strange-attractor.
- Code:
  - `scene::Corruption`: the file block `"corruption"`, its `corruption/<key>` parameters, `packCorruption`, and the
    CPU twin `corruptPoint`. In `src/scene/corruption.{hpp,cpp}`.
  - `Scene::corruption`, and on `Entity`, `corruptTarget` and `corruptEcho`.
  - `FrameUniforms::corruption0..8` (144 bytes, appended last) and `ObjectUniforms::corrupt` (560 to 576 bytes, inside
    the 768-byte slot).
  - `shaders/corruption.wgsl`, included by `pbr.wgsl` (`vs_entity`, the shading, the depth passes) and
    `mesh_wire.wgsl`.
  - The Composition parses, registers and saves the block. When it flattens, it marks the targets and makes the echo
    copies. Each frame it sets the field and the opening from the camera's distance to the focus
    (`updateCorruption`, after the camera).
- Tests:
  - `tests/unit/test_corruption.cpp` (`[corruption][adr1873]`):
    - JSON round-trip and refusals;
    - the field and the opening are monotonic in the camera's distance;
    - inactive is the identity;
    - active is bounded, deterministic and continuous frame to frame;
    - echoes keep only slipped blocks and never the opening;
    - the opening pushes the rock back;
    - the composition registers, marks, makes echoes and drives the live values.
  - `tests/rendering/test_corruption_gpu.cpp` (`[gpu][corruption][adr1873]`):
    - an inactive block renders the bytes of no block;
    - an active block moves the target's pixels, and only the target's;
    - the opening changes them again.

## Context

The owner, after Gate R: "data mosh on the world geometry, not the screen". The rock's surfaces and vertices should
behave like corrupted data:
- strips and blocks that freeze, slip and smear;
- stale copies lagging behind;
- triangles keeping old positions or UVs while their neighbours move;
- strata sliding as if the motion vectors had gone wrong;
- blocks displaced and duplicated in 3D.

All of it lit, shadowed and seen in perspective.

The portal is cut into the rock. It opens from a seam as the camera approaches, monotonically, and a seek must land
on the same state.

Gate R's proof B measured the alternative: image-space mosh over the whole frame was irregular (p90 0.67) where
world-anchored corruption was steady (p90 0.054).

## Decision

Every effect is computed in the entity vertex stage, so the lit pass, the depth prepass and every shadow view agree.
Every one is a function of the vertex's undisplaced world position, the clock and the frame's block.

**The field.** `amount × (1 − smoothstep(inner·radius, radius, |p − focus|))`, scaled each frame by the camera's
approach: `smoothstep(fieldFar, fieldNear, d)²`.

**Blocks.**
- Cubes `block` m on a side are re-picked at `rate` per second, on per-cell staggered epochs (never all at once).
- A share of them (`share` × the field at the cube's centre) slip along the wall, push out of it (`lift`) and slide
  the strata coordinate `uv.x` (`slide`).
- Each slip's envelope eases in, holds and eases out within its epoch, so nothing jumps. The tests bound a frame's
  movement under 2 m.
- The triangles between a slipped block and its neighbours stretch: that is the smear. Inside a block the bands
  slide.

**Strips.** Bands `strip` m tall along the aperture's up slide sideways past each other (`stripSlip`), like slip
faults in the strata.

**Pull and fold.**
- The rock is drawn along the wall toward the opening. The pull is strongest a little outside the lips and zero
  inside them, so the opening keeps its shape.
- A soft fold ripples the wall in and out (`fold`).
- Frozen cells keep only `1 − lag` of the strip slip: stale and lagging.

**Echoes.**
- The Composition adds `echoes` copies (up to 3) of every target mesh within the field's radius. They are the same
  mesh, drawn again, hidden while the corruption does not act.
- Copy k keeps only the slipped blocks (a fragment test on the undisplaced position), at k/(echoes+1) of the way
  back toward where they were.
- The copies are lit and shadowed: stale duplicates trailing behind.

**The opening.**
- Its opening is `smoothstep(openFar, openNear, d)` plus the `open` parameter: monotonic in the camera's approach.
- For the first `seam` of it, a hairline seam of light grows to `width` along the wall.
- Then it splits to `height`:
  - the band just inside the edge folds back into a short tunnel, `depth` m through the rock;
  - the lips fold toward the camera;
  - inside the tunnel the rock is drawn as a black void, and a portal view will be composited there at Gate B
    (`crCut` is kept for it).
- The edge wanders smoothly around the opening and steps on a 2.5 m grid of the wall: torn, not drawn.

**Surface.**
- Glyphs grow with the field.
  - At first, after Gate R, they appeared only in thin bands on walls where the field exceeded 0.35.
  - The owner wanted them more prominent and sooner, as in Gate R's proof 04. Now they cover every face in the field
    from a field of 0.04.
  - The share of lit cells runs from 12% to 72%, and the brightness also grows with the field.
  - Brighter bands travel out of the focus over them.
  - The rock under them darkens with the field, so the code reads.
- Each face reads the world plane it faces most: walls along their horizontal, flats from above.
- Each glyph is a procedural 5×7 cell, our own design with mirrored strokes, cycling and raining (a stateless
  sawtooth after Rezmason/matrix).
- Cell size grows by powers of two with distance, crossfaded between the two nearest, so a glyph is never under
  about 20 px.
- Glyphs are antialiased from the world footprint taken in uniform control flow.
- The water is never a target.
- Targets in the field are drawn double-sided. Their back faces shade dark and faintly warm, the inside of the torn
  rock, rather than letting the sky through.

## Revisions at Gate B (owner and coordinator)

- **The lips.** The lips are torn back as slabs: strata-shaped cells of the wall, 6 × 2.5 m, each a rigid block. They
  are pushed into or out of the rock, drawn outward and lifted by hashed amounts. This replaces the smooth radial
  curl, which read as an even fan of gold petals.
- **The light.** `light` is a point light 10 m in front of the opening. Its intensity is
  `light × (0.12·field + opening²)`, and its colour goes from the glow's to a moon-white as it opens. It lights the
  shadowed face, so the slips and echoes read there.

## Consequences

- With no block, or a block that does not act, the frame is byte-identical (GPU test). Every other entity is
  untouched: a uniform branch on `object.corrupt.x`.
- Velocity is right: the previous frame's vertex is displaced at the previous frame's time (`corruption1.w`).
- The tunnel's walls keep the wall's normals, so they light as the wall did.
- The CPU twin mirrors the vertex displacement. It is the tests' reference, not yet held to the GPU by a parity test.
- The aperture's in-plane basis assumes `axis` is not vertical. A vertical axis falls back to x along the wall.
  Portal 2, in the crater floor, will need that path.

## Revision at Gate C

- `floor` (world y, default off): rest positions below it are never displaced, fading in over the 2 m above it. The
  canyon sets it at the river's surface (-0.8 m), so slipped river-bed blocks no longer push up through the water and
  read as holes (the Gate B weak spot at 23.5 s). It is the tenth vec4 of the frame's corruption block.
- Over a generated ground (`lunarWorld`) the focus's y is a height above that ground (the Moon's floor portal).
