# ADR-139: The volume has two scalability axes, they are tier parameters, and they do not divide evenly

**Status:** Accepted
**Date:** 2026-09-13
**Answers:** Phase F, task F1 — froxel resolution as an explicit tier parameter
**Related:** ADR-032 (the volumetric pass), ADR-035 (tiers scale samples and resolutions only),
ADR-117 (a quality arm), ADR-140 (the two passes are timed apart), ADR-141 (what the axes cost)

## Context

`VolumeRenderer` marched at a hard-coded half resolution — `halfOf(width)`, a private function —
and at whatever `Environment::volumeSteps` the scene authored. Neither was reachable from a tier,
so the one pass that is 63% of Constellation had no policy surface at all: a frame could be made
cheaper only by editing the scene, which is the scene-specific quality reduction §49 forbids.

The phase brief calls this "froxel resolution". The pass is not froxel-based — it is a screen-space
raymarch into a scaled RGBA16F target, and the engine's froxel grid (`light_data.hpp`, 16×8×24) is
a *lighting* structure that §4.7 measured as holding zero lights on Constellation. The parameter
that corresponds to what the brief is asking for is the march target's resolution.

## Decision

Two fields on `QualitySettings`, beside `aoResolutionScale` and `shadowMaskScale`, which they are
the volumetric analogue of:

- **`volumeResolutionScale`** — the fraction of the scene's resolution `fs_volume` marches at.
- **`volumeStepScale`** — a multiplier on the scene's authored `volumeSteps`.

| tier | resolution | steps | what it is |
|---|---|---|---|
| Preview | 0.25 | 0.5 | a sixteenth of the pixels, half the samples |
| Realtime | **0.5** | **1.0** | unchanged: exactly what shipped |
| High | 1.0 | 1.0 | the reference live picture, as `shadowMaskScale` is here |
| Offline | 1.0 | 1.0 | §5.9 — an offline render takes no shortcut |

**They are two axes and not one dial, because they damage different things.** Resolution trades
detail at silhouettes; steps trade banding along the ray. Measured on Glowmere with one axis moved
at a time (`--quality-arm`, §50), `volumesteps` alone produces concentric banding in the smooth sky
and almost nothing at edges, while `volumequarter` alone produces that banding *and* bright fringing
along the mushroom's gills and the plant edges — the depth-aware upsample failing on thin geometry.
A single "volume quality" dial would have hidden which of the two a given tier was paying for.

**A scene may not set either.** The fog a scene authors — its density, colour, height, extent, noise
— is composition and is identical at every scale. How finely it is sampled is quality. §49's rule is
exactly this line, and putting the scale in `Environment` would have crossed it.

## Consequences

**The shipped picture does not move.** `scaledOf(v, 0.5)` reproduces the historical `(v + 1) / 2`
for every `v`, the composite's generalised footprint collapses to the old `×2` mapping when the
ratio is exactly 2, and the captured Realtime frame is **byte-identical** (`cmp`) to the pre-change
binary's on both Constellation and Glowmere. Verified, not reasoned: both binaries were built and
both frames captured under `tools/gpu-lock.sh`.

**At 1.0 the composite becomes an exact copy, not a blur.** `coord` lands on an integer, so the
bilinear fraction is zero, the (0,0) neighbour takes the whole weight, and the pass copies the
pixel's own march. The Offline tier therefore gains a per-pixel volume without gaining a filter over
it — which is what §5.9 means by "no shortcut", and is the only reason moving Offline to 1.0 is a
quality improvement rather than a different picture.

**Three quality arms** (`volumefull`, `volumequarter`, `volumesteps`) and their combination
(`volumepreview`) expose the axes to the A/B harness. Each is a tier's own setting rather than a
fabricated one, per ADR-117.

**What is not decided here.** Whether 0.25/0.5 is the right *Preview*, and whether High should pay
for 1.0, are judgements about what a preview and a reference picture are for. The measurements they
would be argued from are in ADR-141; the artefacts are in the diffs described above. They are stated
as the tier table's defaults, not as findings.

## Alternatives considered

**One `volumeQuality` scalar driving both.** Rejected above: the two axes fail differently and a
combined dial cannot say which failure a tier bought.

**Leave the scale in the scene, with a tier clamp.** Rejected: a scene could then still ask for a
reduction no other scene gets, which is precisely §49's "no scene-specific special cases".

**Scale the march target by a non-uniform factor per axis.** Not built. There is no evidence the
fog's horizontal and vertical detail differ, and §56 says do not add a knob nothing has asked for.
