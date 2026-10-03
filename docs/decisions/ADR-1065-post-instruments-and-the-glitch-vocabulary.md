# ADR-1065: Post effects as instruments: a glitch pass and a display pass, off and byte-identical at their defaults

- **Status:** Accepted (2026-10-02), proto/sonic-garden (VFX expansion, brief §7-8; research 4)
- **Code:**
  - `src/scene/post_glitch.*` (the settings, a table of 30 parameters, the scene-file keys);
  - `fs_glitch` and `fs_display` in `shaders/post.wgsl`;
  - passes 4b and 6c in `PostProcessor::run`;
  - `QualitySettings::postEffectTapScale`;
  - `PostFrameInputs::{renderTime, tapScale}`.
- **Tests:** `tests/rendering/test_post_glitch_gpu.cpp` (`[gpu][post][adr1065]`).
- **Tool:** `tools/sonic_post_fx_sheet.py` (a contact sheet, or `--bench` for a cost table).

## Decision

**Pass `post/glitch`** runs on HDR, beside the lens and before bloom, so the glow follows the damage. It is one pass,
encoded only while one of its amounts is non-zero:

| instrument | parameters | what |
|---|---|---|
| shock | `post/shock/{amount, radius, width, chroma, centerX, centerY}` | a ring that pushes the picture outward ahead of its crest and inward behind, with a chromatic fringe. Drive `radius` from an envelope (`response.kickEnv`, gain -1.2, offset 1.2) and `amount` from the same envelope |
| glitch | `post/glitch/{amount, block, rate, seed, tear, tearShift, swap, drift}` | blocks displaced and channel-swapped, and row bands torn, chosen by hashes of the cell and `floor(time x rate) + seed` |
| split | `post/split/{amount, angle, spectral}` | red ahead and blue behind by `amount` px along `angle`; `spectral` blends to an 8-tap rainbow whose weights keep white white |
| sort | `post/sort/{amount, threshold, length, angle, invert}` | stateless: inside the luminance mask, the brightest (or darkest) sample up the span until the mask ends |
| radial | `post/radial/{amount, centerX, centerY}` | a 12-tap zoom blur |

**Pass `post/display`** runs after the composite and the look, before FXAA:
`post/display/{scanlines, lines, pixelate, posterize, dither}`.
- The mosaic and a 4x4 Bayer dither are indexed by output pixel or cell.
- Posterising is done on `c/(1+c)`, so the steps fall where the eye sees them.
- The scanline count is in lines of the frame, and the darkening keeps the mean.

**Rules for both passes:**
- Pixel sizes are authored at 1080 lines and scale with the output.
- The randomness is a function of timeline time only, so seek equals play with no warm-up (tested: the pattern holds
  within an epoch and re-rolls at the next).
- The Preview tier halves the tap counts (sort 32 -> 16, radial 12 -> 6, spectral 8 -> 4): a sample count, which a
  tier may scale. The amounts and lengths never change with the tier.

## Measured

On the Sonic Garden perc variant at 1920x1080, M2 Max, 240 frames; pass medians at 0.066 ms timer resolution:
- every instrument in `post/glitch` costs at most 0.066 ms, except the spectral split (0.13) and the radial blur
  (0.20);
- `post/display` costs at most 0.066 ms;
- the frame's GPU p50 rises 0.07-0.20 ms per effect;
- at the Preview tier the deltas are within run-to-run noise.

Off costs nothing: the passes are not encoded. The tests check each instrument at its defaults with its shape
parameters moved: byte-identical to the frame without it.

## Rejected

- The exact bitonic sort, the true 8x8 DCT, and a motion-vector datamosh (research 4: cost, or an IIR state that
  ADR-410 forbids).
- **Folding these into the composite:** separate gated passes make "off" structural.
