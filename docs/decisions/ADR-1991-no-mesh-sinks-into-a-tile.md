# ADR-1991: No skinned mesh sinks into a tile

- Status: Accepted (Musical Geography CP-D r2, 2026-11-10), scene/musical-geography.
- Code: the tiles kernel's record placement (`cs_generate_tiles`, `shaders/generator.wgsl`);
  `Composition::groundCharactersOnTiles` (`src/scene/composition.cpp`); the tiles ground as the foot layers' ground
  query (`TerrainGroundQuery::sampleAt`).

## Context

The owner: "when the tile moves back up the feet clip through the tile". His regression frame is
`cpd-r2-feet-close-landings-50-60s.mp4` at 2.0 s (song 52.0 s): the alien in its landing crouch on the just-landed
tile, with both feet and shins inside the tile.

Canaries, in the order they were run:
- **The body's root equals the record's height** (`test_tile_feet_gpu`, to 1 mm), and **the GPU draws the mesh where
  the CPU skin puts it** (`test_pose_vs_draw_gpu`, to 3 pixel rows). Neither was the cause.
- **A static tile (no spring) buries the shins the same way.** Not the spring timing or the swell.
- **Foot IK alone did not lift it.**
- **A top view with and without the ground** at song 51.8-52.1 s: 12,500 to 13,600 alien pixels (the feet and shins)
  hidden by the alien's own tile, while every skinned vertex measured at or above the tile's height.
- So the drawn top was not the tile's height. The kernel placed each box CENTRED on the height
  (`p.y = height - skirt / 2`, stretched by `(thickness + skirt) / thickness`): the top sat half a box thickness above
  the height that bodies stand on, flight clears and the tests compare. That is 0.6 m on the 4 m tiles (1.2 m boxes),
  0.72 / 1.44 / 3.0 m on the coarser rings. The kernel's own comment called `scale.w` "the tile's top"; the box
  disagreed with it, and `test_tile_terrain_gpu` (ADR-1950) had written the disagreement down as the contract.
- **With the top moved to the height:** hidden pixels at the owner's frame fall from 12,504 to 201 (a shading fringe
  that the second fix below does not change). The remaining real sinking is the POSE: the Landing kneel and a hand or
  foot over a neighbour that rings higher, up to 14 cm in 23 of 300 frames at 50-60 s.

## Decision

1. **The tile's height is its drawn top.** The kernel places the box half its stretched thickness below the height
   (`p.y = height - (thickness + skirt) / 2`), so the top is the height for every ring, skirt or none. Bodies, flight,
   the contour pass (`scale.w`) and the tests read one surface.
2. **No pose sinks into it.** After the rigs are posed, each skinned character within 150 m of the camera is lifted by
   the deepest of its low vertices below the top of the tile under that vertex. "Low" means within 0.45 of the mesh's
   foot, plus the hands (well to the side in the bind pose). That is about a third of the mesh, skinned on the CPU and
   cached per mesh; tile tops are cached per cell per frame. The lift moves the drawn mesh only, never the entity
   simulation. It is a pure function of the pose and the ground at this instant, so a seek gives the same result as
   play.
3. The `foot` layers' ground query reads the tiles ground, so a rig with foot IK plants its feet on the tile top
   under each foot.

## Consequences

- **The whole tile field draws half a box thickness lower** (0.6 m at 4 m tiles) relative to everything else in the
  world: the camera, the sky, the underlay. A look that reads `worldPosition.y` (the art agent's colour ramp) shifts by
  that much. The rings now share one top (before, each ring's top sat at its own half thickness above the height), so a
  ring seam no longer steps.
- The measured trace (`cpd-r2-feet2-penetration-trace.png`, every skinned vertex against the drawn top of the tile
  under it, three aliens, 30 fps):
  - before, song 50-60 s: worst 74 cm, sunk in 129 of 300 frames (60 cm whenever standing);
  - kernel fix only: worst 14 cm, 23 of 300;
  - kernel fix and lift: 0 cm in every frame, at 50-60 s and at 140-150 s.
- Tests: `test_tile_terrain_gpu` checks the drawn top against the CPU height (not the record's centre);
  `test_tile_feet_gpu` checks the body against the drawn top and the drawn top against `scale.w`;
  `test_tile_characters` checks every vertex of a hopping alien against the tile under it (needs the uncommitted alien
  assets; skipped without them).
- In the air near a raised tile, the same rule lifts a dangling foot clear of the tile edge.
