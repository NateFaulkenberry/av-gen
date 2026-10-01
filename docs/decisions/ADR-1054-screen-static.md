# ADR-1054: Screen static on SDF surfaces

- Status: Accepted (2026-10-01), proto/liminal-space (All You Got art pass 3, §22)
- Code:
  - `SdfObject::Surface::staticAmount` and `SdfLook::static*` in `src/scene/sdf_object.*`.
  - The surface record's `p1.w`, and `SdfObjectUniforms::look6`, in `src/rendering/sdf_renderer.*`.
  - The static term in `fs_sdf` in `shaders/sdf_raymarch.wgsl`.
- Tests:
  - `tests/unit/test_sdf_static.cpp` (`[adr1054]`).
  - `tests/rendering/test_sdf_gpu.cpp` (`[adr1054]`): snow appears, it moves, the same second gives the same
    image, and it is off by default.

## Context

"All screens inside the world should display static ... analog/digital static ... a recurring visual motif
suggesting that the world is a broken simulation." The world's televisions and monitors are SDF props (the kit's
`tv()`; the `SCREEN` surface). Until now a screen was a flat emissive surface.

## Decision

Static is a per-surface property, so any SDF surface can carry it: the kit's `SCREEN` surface, a monitor, or a
wall made into a screen.

- **`sdf/<o>/surface/<k>/static`** (0..1; 0 = off, the default) mixes the surface's colour and emission with
  snow.
  - The snow is a hash per object-local cell of `look/static/cell` metres (default 0.012), re-rolled
    `look/static/rate` times a second (default 24).
  - A bright bar of strength `look/static/roll` (default 0.35) drifts down the screen.
  - The snow's mean brightness is about 1, so the screen keeps its emission level.
- **It is a pure function of the render time.** A seek shows what play showed, and offline renders stay
  byte-identical.
- All the settings are keyable and routable. For example, a BIG CLAP channel can be routed into `static` for a
  burst of interference, or into `rate` for a frozen frame.
- **JSON:** `"surfaces": [{..., "static": 1}]` and `"look": {"staticCell", "staticRate", "staticRoll"}`. These
  keys are written only when they differ from the defaults.

## Consequences

- The uniform block grows to 272 bytes, within the 512-byte stride. At static 0 the term is skipped and the
  image is byte-identical.
- The term applies to compiled SDF objects with surfaces (ADR-1044), which every kit object is. Mesh-mode SDF
  objects and glTF or procedural meshes do not get it.
- **Cell size and aliasing:** the cells are in object-local metres, so far away they fall under a pixel and
  alias into grey grain, which reads as static anyway. For a TV across a room, a cell of 0.01-0.02 is about right.
