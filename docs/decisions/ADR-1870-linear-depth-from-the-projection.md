# ADR-1870: The linear depth is taken from the projection's depth mapping, not unprojected through invViewProj

- Status: Accepted by the owner (2026-10-30), scene/strange-attractor (canyon water pass 2, 2026-10-29).
- Code:
  - `src/rendering/scene_renderer.{hpp,cpp}`: `FrameUniforms::depthRange` (appended last).
    - x = `-proj[3][2]`, y = `-(proj[2][2] + 1)`, both formed in double from the float matrix the frame was drawn
      with.
  - `shaders/common.wgsl`: the mirrored field, and `linearViewDepth(depth) = x / ((1 - depth) + y)`.
  - `shaders/linear_depth.wgsl`: writes `linearViewDepth` instead of unprojecting `(ndc, depth)` through
    `invViewProj` and dotting with the forward axis.
- Test: `[adr1870]` in `tests/rendering/test_water_depth_forensics_gpu.cpp`.
  - A ground plane seen at a grazing angle from 5 km out, with a far plane of 1e5 m.
  - Every tap's depth is checked against the CPU's ray-plane intersection in double. The target's row noise (the
    second difference against the plane's own) is checked too.
  - The control is the old unprojecting pass, derived from the live file. Row noise: 2.74 m in the control, 0.27 m
    here. The absolute error (under 1%) is the rasteriser's interpolation of a 6 km triangle, the same in both.
- Evidence: `docs/prototypes/strange-attractor/11-canyon-water-2.md`.

## Context

The owner saw black bands flicker in the canyon's river. Two kinds of frame showed it:

- the first seconds;
- 8–10 s, over the left-bank flats.

In those frames the water showed dark diagonal bands, a fine every-other-pixel checkerboard and a ragged bank edge.

**The event.** At 5.067 s, 6.267 s and 0.067 s, the whole river turns into a checkerboard for exactly one frame. The
Laplacian energy over the water jumps 6–10×. The event:

- appears in every earlier clip at the same instants;
- is deterministic, and depends on the camera pose to the sub-frame. A render at t = 5.0666667 s shows it, but
  t = 5.06666 s does not.

**Bisect.** These made no difference: SSR off, the sky mirror off, the cirrus off, the volume off, the depth prepass
off, the pre-ADR-1868 shader. Reflection off removed most of it, but not the checker. Stage-by-stage debug output of the
water shader isolated it:

- the normal, the cube reflection and the sky mirror were identical in the event and the clean frame;
- the SSR's hit coverage changed;
- most of all, the water's **alpha** carried the checkerboard: 24.0 against 2.5.

The alpha comes from the water's thickness over the bed, `bed - viewDepth`, which reads the linear-depth target.
Over the shallow flats the bed is centimetres under the surface. The target's own values there were noisy from pixel
to pixel.

**Cause.** `linear_depth.wgsl` unprojected each depth texel through `invViewProj` and dotted the result with the
forward axis. That is exact in real arithmetic. In f32 it is not, with:

- the camera kilometres from the origin;
- a far plane of 1e5 m.

The inverse rounds, and the depth comes back wrong by up to tens of centimetres at a few hundred metres. The error
differs between neighbouring pixels and changes with every camera pose. ADR-1807 found the same rounding in the sky's
view ray, and ADR-1866 in the shadow cascades. Where the error happens to alternate pixel by pixel, the thickness of
shallow water alternates between "thin, see-through" and "thick, opaque". The dark bed then shows through every other
pixel, in bands that follow the bed's shape.

## Decision

The depth target is computed from the projection's own depth mapping. For `glm::perspectiveRH_ZO`:

- `depth = -A - B / z`, with A = `proj[2][2]` and B = `proj[3][2]`.
- So the view depth is `-B / ((1 - depth) - (A + 1))`.

Both terms are computed so that f32 does not cancel:

- `1 - depth` is exact in f32 wherever precision matters (depth ≥ 0.5);
- `A + 1`, about near/far and a cancellation in f32, is formed on the CPU in double from the float the GPU used.

It is one uniform vec4. The pass has no matrix inverse and no world position.

Every reader of the target benefits: water thickness, SSR, AO, contact shadows, particle fog and picking.

## Consequences

- The 5.067 s event frame goes from 8.7 to 1.3 on the water Laplacian measure; the clean neighbour is 1.3.
- The checkerboard frames are gone from the flight. Over 46 s, dark transients on the water fall from 507 to
  151 px/frame (mean) and from 5,212 to 2,495 (max).
- `[adr1054]` (SDF screen static) asserted byte-identical frames at t = 1.0 and 1.3 with AO on. AO's sampling
  pattern rotates with time, so the claim held only while AO added exactly nothing. With these depths, one centre
  pixel picked up a 1-level AO term. That test now runs with AO off; the static is what it tests.
- No measurable GPU cost: 24.23 → 24.26 ms median; the pass itself 0.10 → 0.09 ms.
- A projection other than a symmetric `perspectiveRH_ZO` would need its own mapping. A scene camera has no other kind
  (ADR-1807 relies on the same fact).
