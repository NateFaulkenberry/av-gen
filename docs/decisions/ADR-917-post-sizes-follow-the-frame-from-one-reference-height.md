# ADR-917: The post chain's sizes follow the frame, from one reference height

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-279 (the bloom's reach is a pixel count: measured, and not changed), ADR-039 (the
energy-conserving pyramid, halation and anamorphic), ADR-040 (tile-based motion blur), ADR-385
(`bloomLevels` is live), Image/Look §68.3 (the look stage's low-pass)
**Found by:** the GV3 revision audit, `docs/glowmere-valley-3/revision/audit/reports/render-post.md`
(engine gap 3; "Post stack" 2, 4, 5 and 6)
**Implemented by:** `PostSettings::referenceHeight` and `post/referenceHeight`, `postPixelScale`,
`planPyramid`, `describePostScale` (`src/scene/post_settings.{hpp,cpp}`); `PostProcessor::run` and
`buildPyramid` (`src/rendering/post_processor.cpp`); the render log line (`src/app/render_job.cpp`)
**Tests:** `tests/unit/test_post_resolution.cpp` (`[post][resolution]`, CPU);
`tests/rendering/test_post_resolution_gpu.cpp` (`[gpu][post][resolution]`)

## Context

The post chain runs at the scene target, so its pixels are the frame's. Some of its sizes followed
the frame and some did not:

| Size | Before |
|---|---|
| defocus, tilt-shift and motion-blur radii, local-contrast radius | pixels at 720 lines, scaled by `height / 720` |
| bloom pyramid depth (its reach) | `bloomLevels` levels whatever the frame: a pixel count (ADR-279) |
| halation pyramid depth | the same count |
| anamorphic streak | `8 * stretch` quarter-resolution texels: a pixel count |
| motion-blur tiles | `tileSize` pixels: a pixel count, while the radius they gather scaled |
| look-stage low-pass | scaled, but cut off at 32 taps, which the scale can reach |

So one project showed a different look at every size. ADR-279 measured the bloom: a 0.3 m source's
halo covered half as much of the frame with `--supersample 2` as without, and it deliberately did
not act, because a fix changes every render at every size except the one a look was tuned at. GV3
now needs that decision taken: its look was tuned on 960x540 x2 previews (a 1080-line chain) and
its final is 3840x2160 x2 (4320 lines), four times further:

- the bloom's reach, the halation and the streak would each cover a quarter of the frame they
  cover in the preview;
- the motion blur's radius scaled to 240 px while its tiles stayed 20 px, and the reconstruction
  only gathers from the 3x3 tiles around a pixel, so every long smear was cut short;
- the audit's workarounds (8 levels at 4K x2, stretch x2, tile 40) only approximate the preview
  and would have to be re-derived for every size.

## Decision

**Every pixel-sized post value is a size at one reference height, `post/referenceHeight`, and the
chain scales all of them by `height / referenceHeight`.** The height is the chain's own, so
supersampling counts: a 960x540 preview at supersample 2 is a 1080-line chain.

- **The default is 720,** the height the already-scaled radii were expressed at, so every one of
  them keeps its meaning exactly. The scale is also what they were already multiplied by.
- **The pyramids** (bloom and halation) are planned by `planPyramid`:
  - At the reference, level k of the authored pyramid carries weight (1 - b) b^k and the coarsest
    level the rest, b being the upsample blend.
  - A frame `octaves` = log2(scale) finer has every level that many octaves smaller, so each
    authored weight moves that many levels towards the coarse end. A fractional shift shares a
    weight between the two levels that bracket its size, which keeps the total at 1 and the weighted
    mean octave exact.
  - The levels finer than the reference's first get no weight: they are downsample steps, and their
    upsample passes are a plain tent (blend 1).
  - A frame coarser than the reference folds the weight of the levels it is too small to have into
    its first.
  - The blends are derived from the weights through suffix sums accumulated from the coarse end, so
    **at a whole-octave shift every step's blend is the authored one to the bit**, and at the
    reference the chain is the pre-ADR-917 chain: the same level count and the same blend on every
    step.
- **The anamorphic streak's reach** is `8 * stretch * scale` quarter-resolution texels. The tap
  count and the source level are chosen from the scaled reach by the existing comb rule, so a long
  streak at 4K reads the same pyramid level, the same fraction of the frame, as in its preview.
- **The motion-blur tiles** are `tileSize * scale` pixels, capped at 160, alongside the radius they
  gather.
- **The look stage's low-pass,** when +-3 sigma exceeds its 32-tap budget, is taken down an octave
  (the same 13-tap downsample) and sigma halved, instead of truncating the gaussian at a fraction of
  a sigma. At the reference nothing changes there.
- **What reached the chain is reported:** `PostStats` gains `pixelScale`, `anamorphicReach`,
  `motionBlurTile`, `motionBlurRadius` and `lookOctaves`. A render logs one line at info saying what
  the scale did, for example "post chain 4320 lines at reference height 1080: every glow, streak,
  halo and blur is 4.00x its authored pixel size... bloom and halation pyramids 6 levels +2.00
  octaves".
- **Where it is set:** `post/referenceHeight` is a direct row at the top of the Parameters panel's
  `post` group, above every section whose sizes it governs. A scene's own `post` block carries it
  as `referenceHeight`.

Rejected:
- **One engine-wide reference, as ADR-915 chose for the water.** The water's fades are anti-aliasing
  thresholds -- where detail becomes finer than a pixel -- so one height suits every project. The
  post chain's sizes are authored looks, and one of them is an integer: a pyramid depth is exact
  only at the height it was tuned at. GV3 tuned six levels on a 1080-line chain; against a fixed
  720 its preview would sit 0.585 octaves off and every level's weight would be shared with its
  neighbour's, so the approved preview could not be kept. A reference per project cannot be used to
  make a preview disagree with its final (ADR-915's objection): both scale from the same number.
  For GV3 the two references coincide at 1080 rows.
- **Keeping the pixel counts and re-authoring each deliverable** (the audit's 8 levels at 4K x2,
  stretch x2, tile 40). Each is an approximation of the preview, has to be re-derived for every
  size, and cannot express a fractional octave.

## Consequences

- **Every scene with bloom on (the default, so nearly all of them) looks different at any chain
  height other than its reference.** With the default reference of 720:
  - At 1080 lines (1080p, or a 540-line preview at supersample 2) the bloom's reach, the halation,
    the streak and the motion-blur tiles are 1.5x the pixels they were: the same fraction of the
    frame as at 720.
  - At 2160 lines (1080p x2) they are 3x; at 4320 (2160p x2) 6x.
  - Below 720 they shrink the same way.
  - The scenes that also use the streak, halation, motion blur or the look stage, each changing
    the same way: `city/night-shift`, `composition/glowmere-lyrics`, `effects/ufo-stack`,
    `hero/hero`, `hyperspace/hyperspace`, `imagelook/il-look`, `imagelook/il-wide-tier`,
    `infinite/infinite`, `machine/machine`, `reassembly/reassembly`, `weather/ashfall`,
    `weather/fireflies`, `world/glowmere-atmospherics`, `world/glowmere-stylized`,
    `world/glowmere-valley-2` (with its `-multicam` and `-song` variants), `world/grove`,
    `world/world`, and the ten `world/_diag-water-*` and `world/_pre-defects` diagnostics.
  - A scene tuned at another height keeps its look there by setting its reference to that height.
- **Nothing moves at a chain height equal to the reference.** Verified on Glowmere Valley 3's
  preview (see below).
- **GV3** should set `post/referenceHeight` 1080, its previews' chain height, and
  `post/motionBlur/maxRadius` 60 (from 40), which keeps the radius it had at 1080 lines when the
  radius was expressed at 720. Its 960x540 x2 previews are then unchanged, and its 1080p x2 and
  2160p x2 finals show the preview's look at 2x and 4x. `dof/maxRadius`, `tiltShift/maxRadius` and
  `look/localContrastRadius` are off in GV3 today; if they are turned on, their values are now
  pixels at 1080 lines.
- **Tests re-baselined:** see the list recorded below.
- **Cost at 4320 lines** (reference 1080, pixel scale 4): recorded below.
