# ADR-1401: A procedural object can set its normal step

- Status: Accepted (2026-10-16), proto/cymatic-world
- Code: `scene::ProceduralGeometry::normalStep` (`src/scene/procedural.{hpp,cpp}`, JSON `normalStep`), the draw's
  `timeInfo.z` (`src/rendering/procedural_renderer.cpp`), read by `vs_proc` (`shaders/procedural.wgsl`).
- Test: `tests/unit/test_procedural_normal_step.cpp` (`[normal-step]`).

## Context

A deformed procedural vertex rebuilds its normal from three deformed points `eps` apart, and `eps` was always
1/1000 of the source mesh's radius. Cymatic World's ground is one 5 km mesh (dense near the basin, coarse to the
horizon), so `eps` was 3.7 m while its vertices near the camera are 0.36 m apart and the resonance draws 5 m cells:
the normals averaged across whole cells and came out sideways or flipped, which rendered as ragged black patches in
every crater (measured: present with shadows off and with the material program removed; gone with a 0.12 m step).

## Decision

`normalStep` (metres, default 0 = the old automatic step, unwritten): when set it is the finite-difference step.
A per-draw uniform, not structural. Every existing scene is unchanged.

## Consequences

- A large mesh deformed by fine features can be lit correctly; small meshes need nothing.
- The step should be about the vertex spacing where the camera looks; finer than the spacing gives facet noise.
