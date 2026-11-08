# ADR-1977: Output dither, so slow gradients never band

- Status: Accepted (Musical Geography Phase 2, 2026-11-08), scene/musical-geography. This is request A of
  `docs/prototypes/musical-geography/02-requests.md`, folded into 1977.
- Code:
  - `PostSettings::dither` (`post/output/dither`, 0..2, default 0), in `src/scene/post_settings.{hpp,cpp}`;
  - `TonemapUniforms::upscale[2]`, in `src/rendering/scene_renderer.{hpp,cpp}`;
  - the dither itself, in `fs_main`, `shaders/tonemap.wgsl`.

## Context

A slow sky or fog gradient spans few 8-bit steps across many pixels and shows as bands. The art agent's mock-ups
banded at dusk and night. The engine had no dither; film grain stood in for one, at a visible cost.

## Decision

A triangular-PDF dither (the sum of two uniform hashes, −1..1) of `dither` 8-bit steps, added per channel and per
pixel to the encoded output. It goes after the sRGB encode and before anything quantises the frame, and both the
swapchain and the encoder read that output. The hash is keyed on the frame nonce, so an offline render repeats
exactly. At 0, the default, the frame is byte-identical to before.

## Consequences

- One LSB of triangular noise is below what the eye reads as grain, and it removes banding in an 8-bit output.
- The fixture project sets `post/output/dither` to 1. The art agent's scenes choose their own value.
