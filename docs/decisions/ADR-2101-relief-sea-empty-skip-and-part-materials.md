# ADR-2101: A cloud sea with relief, an empty-air skip, and per-part materials by glTF name

- Status: Accepted (2026-11-19), avagent/neon-dreams (Neon Dreams, iteration 3)
- Extends: ADR-2100 (Cloud Environment v2), ADR-044 (mesh sources), ADR-030 (material programs).
- Code: `src/scene/clouds.{hpp,cpp}` (`CloudSea::relief|reliefScale|billow|farFade|breaks`),
  `src/rendering/cloud_renderer.{hpp,cpp}` (uniforms `sea3..sea6`, the bins' packed height range),
  `shaders/clouds.wgsl` (`seaSurface`, `seaShadow`, `emptySkip`), `shaders/clouds_resolve.wgsl` (the uniform block),
  `src/scene/procedural.{hpp,cpp}` (`ProceduralGeometry::partMaterials`), `src/scene/composition.cpp`
  (`resolveMaterial` applies them).
- Tests: `tests/unit/test_clouds.cpp` and `tests/unit/test_procedural.cpp` (`[adr2101]`); the bins test in
  `tests/rendering/test_clouds_v2_gpu.cpp` now also checks each column's packed height range.

## Context

Iteration 2's sea was a height band whose density was a coverage map times a 3D noise. From a camera 150-300 m above
it, it read as a flat dark ground plane with a ruler-straight horizon: no crests, no troughs, nothing for the moon to
rim. Two defects hid behind that look:
- **The march ran out of steps before it reached the sea.** With the eye inside the lobes' bounding box (which the far
  cumulus towers make kilometres wide), every ray started at t = 0 and stepped through empty air in steps that grow
  with distance. 72 steps covered about 1.5 km; the sea, 2-10 km away at grazing angles, was never sampled. Without
  lobes the march started at the sea's top and the sea appeared.
- A building's window light came from a world-space program on boxes. On imported buildings a world grid cannot line
  up with modelled panes, and a node's `material.program` ran on every part of a multi-material GLB.

## Decision

1. **Relief sea** (`sea.relief > 0`). The sea is solid below a top surface: one swell of `reliefScale` metres plus
   three octaves of billow noise (|n| inverted: rounded crests, creased troughs), +-`relief` metres about `top`, bumped
   in 3D by `billow` metres of the shape noise. `breaks` (at most two ellipses: centre, radius, elongation, yaw) lower
   the surface to the floor; `coverage` below 1 still opens noise breaks. With `lightSteps` 0 the key's shadow comes
   from the height field itself (`seaShadow`, four taps toward the key), so crests are lit and troughs dark at the cost
   of 2D taps. `farFade` fades the last share of `maxDistance` so the horizon dissolves into the haze rather than
   ending. `relief` 0 is the ADR-2100 band, unchanged.
2. **Empty-air skip.** Each lobe bin's count word now packs the column's vertical extent (count in bits 0-7, the lowest
   lobe bottom in 8-19, the highest top in 20-31, quantised over the lobes' box). A sample above the sea band in an
   empty column, or above or below its lobes, jumps to the nearest of the column's exit, the column's height band and
   the sea's top. `lobeBase` also returns at once outside the column's height range.
3. **`partMaterials`** on a procedural node: `{"<glTF material name>": {"program", "emissiveColor",
   "emissiveIntensity"}}`. A named part takes that program instead of the node's, and that emissive colour and
   strength (which multiply its emissive texture as the glTF factor does). Parts not named keep the node's program and
   their own factors. Keyed by name, not by the area-ordered part index, so an asset re-export does not silently move
   an override onto a different part.

4. **`kMaxProceduralObjects` 256 -> 1024.** Every material part of a multi-material asset is a procedural object; the
   art agent's layout of the Mooring (606 instances of 93 assets) flattens to 598 objects, and past 256 the renderer
   skipped the rest with one warning (decks, the pier and the Great Lantern vanished). The slots are 256-byte dynamic
   offsets into one buffer, so only the buffers grow. `[procedural]` render tests pass.

## Consequences

- Neon Dreams' sea has crests and troughs to the horizon; the march reaches it in a handful of steps. The skip also
  makes the cloud pass cheaper wherever the lobes are sparse (measured in the Neon Dreams iteration-3 notes).
- A window program can run on a kit's pane materials only (`glass`, `win_warm`, `win_cool`) and read the panes' own
  UVs, so the lit pattern always sits on a modelled pane; each sign node gives its `neon_a` part its own colour.
- Found, not fixed: a procedural mesh with `alphaMode: blend` and a program opacity still draws opaque, so the
  lighthouse beam cones are built (`build.py --beams`) but left out.
- Not done: a per-instance colour for one asset (the points distribution has no colour column), so one sign colour is
  one node.
- Found, not fixed: a node's `position` parameter clamps at +-10 km (`nodes/<n>/position`), so a far object (a moon at
  60 km) must be placed by its distribution point rather than by its node position.
