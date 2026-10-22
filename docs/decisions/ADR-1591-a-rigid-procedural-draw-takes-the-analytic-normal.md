# ADR-1591: A rigid procedural draw takes the analytic normal

- Status: Accepted (perf/rainbow-world-live)
- Amends ADR-1021 (the finite-difference normal and its handedness) and ADR-1492 (which evaluations a draw skips).
  Behaviour-preserving to float rounding; the measured image difference is below.
- Code: `shaders/procedural.wgsl`, `procVertex`: `needNormal` is false for a draw that nothing can bend, which
  then takes `nRef` as its normal.
- Tests:
  - `tests/rendering/test_procedural_gpu.cpp`, "Twist deformer: amount 0 is the identity". A twist at amount 0 is an
    enabled deformer, so it takes the finite-difference path and the plain column the analytic one. The case compared
    their hashes and now requires no channel to move by more than one level and under 1% of channels to move at all.
    A disabled deformer leaves the draw rigid, and that check is still bit-exact.
  - The rest of the procedural GPU tests: deformers, wind, FXL displacement, mirrored instances, LOD, culling, the
    material and compiled-material cases, ADR-1021's handedness. Also the `[adr1590]` case, a masked rigid draw in
    depth.

## Problem

A procedural mesh vertex computes its normal by finite differences. It evaluates the deformer chain at the vertex
and at two neighbours a small step along the surface, then crosses the differences. That is the only way to get the
normal of an arbitrarily deformed surface, and it costs two extra chain evaluations per vertex in the lit pass.

Rainbow World's live scene has no deformers at all: its clouds, ground bands, rocks, trees, grass and flowers are all rigid
instances. Its late song is geometry-bound. At High, render scale moves about 30% of the frame, and the clouds still
cost 3.3 ms at Emergency's 0.38 scale. The two evaluations were a large part of the vertex stage.

## Decision

The finite-difference normal is taken only when something can bend the surface:
- an enabled deformer slot (a disabled one is packed with code -1 and moves nothing);
- the wind bend (`windSway.w`);
- an FXL displacement (`fxA.z` with any of `FX_DISPLACE`'s bits).

All three are uniform per draw. Otherwise the chain from the source to the world is the instance and the object
matrix, an affine map `J`. The finite-difference normal of an affine map is `normalize(det(J) J^-T n)`, made outward
by the handedness sign (ADR-1021), which is `normalize(J^-T n)`. That is exactly `nRef`, which the vertex stage
already computes as the reference hemisphere.

Point sources and fibers are unaffected: they return before this code. The velocity evaluation is unchanged.

## Consequences

- **Measured** on Rainbow World with ADR-1590 in (headless High, GPU median, two interleaved runs each, on AC; as an
  experimental shader against the same binary):

  | Window | Before | After |
  |---|---|---|
  | 144-158 s | 29.0-29.2 ms | 25.7 ms |
  | 185-199 s | 24.8 ms | 22.4 ms |

  The scene pass falls by about 3.3 ms. Every scene with rigid procedural meshes gets the same kind of saving, in
  proportion to its vertex count.
- **The image is the same to rounding.** The cross product of two differences and the normal matrix are different
  float paths to one vector.
  - Offline Rainbow World stills at 22, 62, 113, 150, 175 and 205 s (the offline renderer is deterministic: two
    renders with the old shaders are byte-identical):
    - 0.03-0.15% of pixels differ, by 1/255;
    - 1-7 pixels a frame by more than 2/255.
  - Two isolated pixels at 150 s differ by more than 8/255 (46 at most): sub-pixel coverage flips on far geometry.
  - Nothing differs to the eye.
- The finite-difference path is unchanged for everything that bends, so ADR-1401's authored step and ADR-1021's
  handedness still apply there.
