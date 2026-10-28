# ADR-1807: The sky, cloud and atmosphere rays come from the camera basis

- Status: Accepted (eng/render-improvements, 2026-10-28). Render audit recommendation R1 (gap G1), approved by the
  owner.
- Code: `shaders/common.wgsl` (`cameraViewRay`; `cameraRight.w`, `cameraUp.w`), `shaders/skybox.wgsl` (`fs_sky`),
  `shaders/clouds.wgsl` (`fs_clouds`), `shaders/atmosphere.wgsl` (`fs_atmosphere`), `src/rendering/scene_renderer.cpp`
  (the frame uniforms), `src/rendering/scene_renderer.hpp`.
- Test: `tests/rendering/test_sky_rays_gpu.cpp` (`[gpu][sky]`).

## Problem

The skybox, the cloud pass and the atmospheric sky built each pixel's view ray by unprojecting two points through the
inverse view-projection, in f32:

    rd = normalize(invViewProj * (ndc, 1, 1) / w  -  invViewProj * (ndc, 0, 1) / w)

That is exact in real arithmetic and poor in f32 at large far planes and far from the origin: the inverse of a
projection with a 1e5 m far plane, translated kilometres from the origin, rounds both points coarsely, and the
difference of two large, nearly equal vectors keeps the rounding. The render audit's model
(`scripts/f32_ray_error.py` on `research/render-audit`, the real `glm::perspectiveRH_ZO` and an f32-rounded matrix),
in 1080p pixels:

| Case | p50 | p99 | max |
|---|---|---|---|
| Aurora as authored (near 1, far 45,000, camera about 1 km out) | 0.036 | 0.105 | 0.131 |
| Default free camera (near 0.1, far 2,000) | 0.016 | 0.048 | 0.068 |
| Large world (near 0.1, far 1e5, camera 5 km out) | 2.17 | 6.83 | 9.06 |
| Stress (near 0.05, far 1e6, camera 20 km out) | 10.6 | 39.2 | 56.0 |

Invisible in today's scenes; in a large world, view-dependent jitter in the stars, the clouds and the sky gradients
that moves as the camera does. The camera-basis ray is exact to 0.0001 px in every case. ADR-707 already fixed the same
defect in the volume march (`volume.wgsl` `viewRayDirection`).

## Decision

- **`cameraViewRay(ndc)` in `common.wgsl`**: `normalize(forward + ndc.x * tanX * right + ndc.y * tanY * up)`, from
  the camera basis the frame uniforms already carry.
- **The two tangents ride in the basis vectors' spare lanes**: `cameraRight.w = 1 / proj[0][0]` (tan of the half
  horizontal field of view) and `cameraUp.w = 1 / proj[1][1]` (vertical). The `w` lanes were 0 and read by nothing;
  no offset in `FrameUniforms` moves, so nothing else changes size or layout. The shadow views overwrite both vectors
  with their own (w = 0); no sky is drawn in a shadow pass.
- **The skybox, the clouds and the atmosphere use it.** The xyz of the basis is unchanged (`invView` columns), so the
  billboards, GTAO and every other reader see the same values as before.
- It assumes a symmetric perspective, which is the only projection a scene camera has (`Camera::projection`). An
  off-centre projection would need its two shear terms added here, as `volume.wgsl` already notes.

**Not changed here** (audited, kept): the other `invViewProj` users. `volume.wgsl` already uses the basis (ADR-707).
`linear_depth.wgsl`, `post.wgsl` and two sites in `distortion.wgsl` reconstruct *points* at a measured depth, where
the error is relative to the point's distance and harmless. `sdf_raymarch.wgsl` (near and far points),
`distortion.wgsl` and `ecosystem.wgsl` (far point minus camera) and the Astral renderer build *rays* the old way. They
march finite, nearby content at today's scales; they are the candidates if a large-world SDF, distortion or water
streak ever shows the same jitter, and `cameraViewRay` is the fix.

## Consequences

- **The regression test** renders the Deep Space star field from a camera at the origin (near 0.1, far 2,000) and
  from the audit's stress camera (20 km out, near 0.05, far 1e6) looking the same way. The sky is at infinity, so the
  two frames must match: **0** visibly different pixels with this change. **Negative control:** with the old
  unprojection restored in `skybox.wgsl` the same test reports **6,910** (of 518,400; 423 star pixels in the frame)
  and fails.
- The test places the camera's target 10 km down the view: `target - position` with a target 1 m from a camera 20 km
  out is itself off by about a milliradian in f32 (the camera's error, not the sky's). That is a latent large-world
  camera issue in the same family, recorded here, not fixed.
- **Today's scenes** (picture comparison, ADR-1810: main's binary at `9b4156ea` against this change, offline renderer,
  960x540, three frames each of the five reference live scenes; share of pixels moved by more than 2 levels / mean
  change in levels / largest change):
  - Rainbow World, Rift Live, Talking Heads: SAME or WITHIN on all nine frames (0.000% / 0.000 / at most 1 level, one
    isolated 9-level pixel on Rainbow). These scenes' skies are image-identical to rounding.
  - Event Horizon: WITHIN on all three (0.001-0.006% / 0.006-0.015 / 17-32): a few dozen scattered star-edge pixels.
  - **Aurora Borealis: DIFFERS** (t19 0.56% / 0.08 / 127; t32 5.37% / 0.50 / 203; t45 3.13% / 0.25 / 153). This is
    **not** a change of look. The aurora ribbons dither their plane march with a jitter hashed from
    `floor(rd / pixelAngle)` (ADR-1641, `atmosRibbonJitter`), so a pixel whose ray sits on a cell boundary changes
    cell when the ray moves by one ulp, and gets another random offset. The difference heatmap is exactly the ribbons'
    speckled edges; side by side at 3x the grain is the same noise in another arrangement, with the same density,
    colour and placement of the ribbons. The audit's acceptance ("1/255 or less on fewer than 0.1% of pixels") holds
    for four scenes; for Aurora it is met in kind (the same picture) but not in bytes, and that is recorded here as a
    documented-equivalent change. With the old ray that dither also moved with the camera's position by rounding.
- Clean re-capture of this build: SAME on 13 frames, WITHIN (max 1 level) on two Rift frames.
- Review (exact files) in `~/Desktop/av-gen-review/103-render-improvements/r1-sky-rays/`:
  `stress-camera-20km-OLD-ray.png`, `stress-camera-20km-NEW-ray.png`, `home-camera-reference.png` (the test's frames);
  `aurora-borealis-t032.00-BEFORE.png`, `aurora-borealis-t032.00-AFTER.png`, `aurora-t32-crop-before-after.png` (3x
  crop), `aurora-t32-difference-heatmap.png`; `event-horizon-t067.00-BEFORE.png`, `event-horizon-t067.00-AFTER.png`.
