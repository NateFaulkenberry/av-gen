# ADR-1818: Specular keeps its multiple-scattering energy (rough metals brighten)

- Status: Accepted (eng/render-borrow, 2026-10-29). Filament evaluation borrow item A5, approved by the owner. It
  changes pictures on purpose.
- Code: `shaders/pbr_shade.wgsl` (`specularEnergyCompensation`; the direct and IBL specular), `shaders/common.wgsl` and
  `src/rendering/scene_renderer.{hpp,cpp}` (`frame.lightCounts.w`), `src/rendering/render_quality.hpp`
  (`QualitySettings::multiScatter`), `src/rendering/quality_policy.hpp`, the `nomultiscatter` arm.
- Test: `tests/rendering/test_gltf_glass_gpu.cpp` (`[adr1818]`).

## Problem

A Cook-Torrance microfacet BRDF models one bounce off the microfacets. Light that bounces between microfacets before it
leaves is dropped, and the loss grows with roughness: a rough metal reflects visibly less than it should and reads as
dull and dark. The Filament evaluation's material lab showed it on rough gold and steel, which Filament compensates and
AV Gen did not.

## Decision

- **Fdez-Agüera's compensation (2019), as Filament applies it**: every specular lobe is scaled by
  `1 + f0 (1 / Ess - 1)`. Ess is the directional albedo of the single-scattering lobe for f0 = 1, which is the split-sum
  LUT's A + B at (nDotV, roughness). Smooth surfaces have Ess near 1 and are unchanged; rough metals (high f0, low Ess)
  gain the most; dielectrics (f0 0.04) barely move.
- **Ess comes from the LUT where there is an IBL** (the same integral the IBL specular already uses), and from Karis's
  analytic fit of that LUT otherwise, so the direct lights of a scene without an environment are compensated too.
- **Applied to the direct specular** (every light, LTC area lights included) **and the IBL specular**. Toon, stylised and
  unlit paths are untouched.
- **On in every tier.** `QualitySettings::multiScatter = false` (the `nomultiscatter` arm, `frame.lightCounts.w = 0`) is
  the picture before this ADR, and the one-line revert.

## Consequences

- `[adr1818]`: a rough (0.8) gold sphere under one light and no IBL brightens by 17% in mean luma (90.6 -> 106.3); a
  smooth (0.1) plastic sphere moves 0.002%.
- **The reference scenes** (`tools/picture_check.py`, offline, 960x540, three frames each, compensation off against on,
  the same binary through the `nomultiscatter` arm; `--tol 1`):

  | Scene | Pixels beyond 1 level | Largest change | Where |
  |---|---|---|---|
  | Rainbow World | 0.10-7.70% | 21 levels | spread over the lower two-thirds of the frame (the ground and props); not attributed per material |
  | Talking Heads | 1.66-1.86% | 29 levels | the metallic busts: brighter in their mid-tones (the crop) |
  | Event Horizon | 0.00-0.003% | 2 levels | |
  | Aurora Borealis, Rift Live | 0.00% | 1 level | |

  The material lab (Filament S1, 1080p, realtime tier): at most 14 levels, on 0.15% of pixels beyond 2 levels; the region
  that changed most is its floor at a grazing angle, so in this lab the effect is small. The evaluation asked for rough
  gold and steel to brighten toward Filament's; `[adr1818]` shows the rough-gold case (+17%), and a side-by-side against
  Filament was not rendered here. Review: `~/Desktop/av-gen-review/103-render-improvements/a5-rough-metal/`
  (`*-OFF.png`, `*-ON.png`, and `*-crop-off-on.png`, each crop the region that changed most).
- **Cost: none measurable.** `--ab nomultiscatter`, 1080p, interleaved: the lab +0.08 ms (inside a 2% floor), Aurora
  +0.05, Talking Heads +0.01, Rift -0.11, Event Horizon +0.43 (inside a 10.6% floor), Rainbow World +0.16; every one
  inside its noise floor. One LUT read (or the fit) and a multiply per pixel.
