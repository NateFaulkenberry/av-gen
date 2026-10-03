# ADR-1075: The letterbox, camera roll, and tiny scales that no longer pop to full size

- **Status:** Accepted (2026-10-02), proto/sonic-garden (the Sonic Abstract art agent's engine needs 1, 2 and 5,
  `docs/prototypes/sonic-garden/PROGRESS-abstract.md`)
- **Code:** `displayLetterbox*` in `src/scene/post_glitch.*` and `fs_display` (`shaders/post.wgsl`);
  `camera/roll` in `src/scene/composition.*`; `Transform::fromMatrix` in `src/scene/scene.cpp`.
- **Tests:** `[gpu][letterbox][adr1075]` (`tests/rendering/test_toon_gpu.cpp`), `[camera][roll][adr1075]`
  (`tests/unit/test_camera_roll.cpp`), `[transform][tinyscale]` (`tests/unit/test_transform_tiny_scale.cpp`).

## Decision

1. **Letterbox.** `post/display/letterbox` (an aspect, width / height, e.g. 2.39; 0 = off) blacks bars top and bottom
   when the aspect is wider than the frame, left and right when narrower. `post/display/letterboxAmount` (0..1,
   default 1) slides them in, so a route or a track can open or close them. Scene `post` keys `displayLetterbox`,
   `displayLetterboxAmount`. Part of the display pass (after the grade, before FXAA); off and byte-identical at 0.
   UI: Parameters panel, `post` group, `display` section.
2. **Camera roll.** `camera/roll`, degrees about the line of sight (positive rolls the camera clockwise). Only the
   camera's up turns: the world and its lights stay where they are, which is what turning the SDF object could not
   give. 0 is the world's up exactly. UI: Parameters panel, `camera` group.
3. **Tiny scales.** `Transform::fromMatrix` fell back to the identity (keeping the position) whenever
   `glm::decompose` refused a matrix with a determinant under float epsilon -- any uniform scale below about 0.005 --
   so an object shrunk toward zero (the parameters' hard minimum is 0.001) drew at full size and unrotated. The
   fallback is now a direct affine decomposition: column lengths (one negated for a mirror), normalised columns,
   re-orthogonalised from the longest axis. A true zero scale keeps the identity rotation and a zero scale.

## Consequences

- Parking an object at scale 0.001 now hides it as expected; the art agent's 0.01 workaround can go.
- glm::decompose still handles every matrix it accepts, so nothing that drew correctly changes.
