# ADR-1607: A reduced frame is upscaled, not stretched

- Status: Accepted (eng/living-prism-p1, The Living Prism P1-6). Amended by ADR-1613: `upscaleSharpness` is now the post
  parameter `post/output/upscale-sharpness`.
- Amends ADR-137 (the tonemap samples the HDR target bilinearly when `renderScale < 1`). Byte-identical at
  `renderScale` 1.
- Code: `shaders/tonemap.wgsl` (`upscaledHdr`), `src/rendering/scene_renderer.{hpp,cpp}` (`TonemapUniforms::upscale`,
  48 bytes; the quality arm `bilinearupscale`), `src/rendering/render_quality.hpp` (`spatialUpscaler`,
  `upscaleSharpness` 0.2).
- Tests: `tests/rendering/test_upscaler_gpu.cpp` (`[adr1607]`): at scale 1 the frame is the same with the upscaler on or
  off; at 0.5 it is sharper than bilinear (edge energy 159,747 vs 145,218 at 256x256) without moving the mean.

## Problem

The live ladder lowers `renderScale` to 0.85, 0.71, 0.5 and below (ADR-1083). The tonemap brought the smaller target up
to the output with one bilinear fetch: soft, and its edges stair. 03 §10 P1-6 recommended a spatial upscaler, so that a
lower rung costs less sharpness.

## Decision

When the scene target is smaller than the output and `spatialUpscaler` is on (the default), the tonemap's fetch becomes,
in the same pass:
1. a Catmull-Rom (bicubic) reconstruction in nine bilinear taps;
2. clamped to the four source texels around the sample: FSR 1 EASU's anti-ringing rule, so a bright edge does not
   overshoot into a halo;
3. sharpened contrast-adaptively over the cross one source texel away, as RCAS/CAS do, in the tone-compressed space
   x/(1+x), so HDR highlights neither ring nor dominate the weights; `upscaleSharpness` 0..1 (0.2 default).

This is FSR 1's relative, not FSR 1: EASU is a 12-tap edge-directed Lanczos and RCAS a separate pass. One pass and no
direction analysis keep it cheap and the code small. At a matched size the fetch is the bilinear one, byte for byte.

## Evidence

- The Living Prism blockout at Low (scale 0.5), headless, 1080p: the tonemap pass 0.08 ms bilinear, 0.19-0.32 ms
  upscaled; GPU p50 9.34 vs 9.60 ms on the second, settled repetition (`83-living-prism-p0/P1-6/`).
- The crop (`crop-bilinear-vs-upscaler.png`): facet edges and the crystal tips sharper; at 0.5 the sharpening also shows
  more of the reduced frame's own aliasing on the pool's ripples. That is a taste call for the art pass
  (`upscaleSharpness`), not a defect of the reconstruction.

## Consequences

- Every live scene below Ultra looks sharper at the same cost; offline renders (scale 1) are unchanged.
- No temporal component: TAA/TAAU remains the P2 spike, only if the blockout sits below 0.7.
