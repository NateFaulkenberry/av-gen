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
`buildPyramid` (`src/rendering/post_processor.cpp`); `fs_box_down`, `fs_velocity_tile_max_rows` and
`fs_velocity_tile_max_columns` (`shaders/post.wgsl`); the render log line (`src/app/render_job.cpp`)
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
- **The pyramids** (bloom and halation) see the frame at the reference's size:
  - **A frame whole octaves finer than the reference is box-filtered down by them first,** colour
    and emission target alike (`fs_box_down`, one bilinear tap a texel, exactly the mean of four).
    That is the relation between a supersampled final and the preview it was tuned on. Both
    prefilters are boxes of the frame followed by a soft threshold (bloom's a 2x2, halation's a
    4x4), so after the box they threshold the same boxes of the picture the reference's do, and
    every level after is the reference's own: the same count, the same blends, the same sizes.
  - **The fraction of an octave that is left** (and any shift of a frame coarser than the
    reference) is planned by `planPyramid`. At the reference, level k carries weight (1 - b) b^k and
    the coarsest level the rest, b being the upsample blend; a shift moves each authored weight that
    many levels towards the coarse end, a fractional one sharing it between the two levels that
    bracket its size, which keeps the total at 1 and the weighted mean octave exact. A coarser frame
    folds the weight of the levels it is too small to have into its first. The blends come from the
    weights through suffix sums accumulated from the coarse end, so at the reference -- and at any
    whole-octave shift -- every step's blend is the authored one to the bit.
  - At the reference the chain is therefore the pre-ADR-917 chain: no box, the same level count and
    the same blend on every step.
  - Rejected, after measuring it: building the extra octaves as pyramid levels (weightless fine
    levels, downsampled and upsampled through). Upsampling through a weightless level is a tent and
    nothing else, and a subtractive threshold takes a larger share from the edge of a small source
    when its boxes are coarse, so the finer frame's halo carried 1.4x the energy and at 4x the
    reference the halation's core was twice as wide as its preview's (relative L1 0.85, worse than
    the unscaled chain's 0.78). Boxing first brought it to 0.136 (the measurements below).
- **The anamorphic streak's reach** is `8 * stretch * scale` quarter-resolution texels. The tap
  count and the source level are chosen from the scaled reach by the existing comb rule, so a long
  streak at 4K reads the same pyramid level, the same fraction of the frame, as in its preview.
- **The motion-blur tiles** are `tileSize * scale` pixels, capped at 160, alongside the radius they
  gather. The reconstruction (McGuire 2012) gathers along the velocity of a pixel's 3x3 tile
  neighbourhood, so a pixel more than a tile or two from a moving object never learns it moved:
  tiles that stayed 20 px while the radius grew to 240 cut every long smear short.
  - **Tiles past 40 px** (the parameter's old hard maximum) are reduced in two passes, the longest
    velocity along each tile-wide run of a row and then the longest of those down each tile-tall
    column. The single pass reads a tile's area serially per texel: at 80 px it took the motion blur
    from 13.8 to 47.8 ms a frame at 7680x4320. The two passes find the same vector (the runs are
    kept at the tiles' RG16F, so they could differ only in a tie within half-float precision; the
    measured image did not differ, below). 40 px and under keep the single pass, so every chain at or
    below twice its reference is unchanged.
- **The look stage's low-pass,** when +-3 sigma exceeds its 32-tap budget, is taken down an octave
  (the same 13-tap downsample) and sigma halved, instead of truncating the gaussian at a fraction of
  a sigma. At the reference nothing changes there.
- **What reached the chain is reported:** `PostStats` gains `pixelScale`, `pyramidBoxOctaves`,
  `anamorphicReach`, `motionBlurTile`, `motionBlurRadius` and `lookOctaves`. A render logs one line
  at info saying what the scale did, for example "post chain 4320 lines at reference height 1080:
  every glow, streak, halo and blur is 4.00x its authored pixel size... bloom and halation read the
  frame box-filtered 2 octave(s) down, then 6 levels +0.00 octaves, anamorphic reach 83 -> 332
  quarter-res texels, motion-blur tile 20 -> 80 px and radius 60 -> 240 px".
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
- **Measured** (`test_post_resolution_gpu.cpp`): one shot's effect contribution -- the frame with
  the effect minus the frame without it -- at 320x180 against 1280x720 box-filtered down 4x, with
  the reference at 180 lines (the treatment), and with each frame its own reference plus the motion
  blur's old height / 720 radius (the chain before ADR-917, the control):

  | Effect | Relative L1, treatment | control | Reach (r90) ratio, treatment | control |
  |---|---|---|---|---|
  | bloom | 0.073 | 0.498 | x1.003 | x0.286 |
  | halation | 0.136 | 0.782 | x1.005 | x0.295 |
  | anamorphic streak | 0.051 | 1.439 | x1.000 | x0.325 |
  | motion blur (smear saturated at the radius) | 0.094 | 0.355 | | |
  | look stage (local mean at 4x its budget) | 0.029 | 0.310 (throwaway build: truncated gaussian) | | |

  What remains in the treatment is rasterisation: a box 5 px across at 180 lines is drawn with a
  stair at its edge that 720 lines resolves. At one frame size, halving the reference moves the
  glow's reach from 0.034 to 0.062 frame heights (x1.81).
- **The separable tile maximum** gave the single pass's motion-blur error to all twelve printed
  digits (a throwaway build that took every tile in one pass), so the two find the same vectors.
- **Cost at 4320 lines,** the post chain at 7680x4320 with GV3's settings (the hidden `perf:` case,
  48 frames, median): **15.1 ms at reference 1080** (pixel scale 4: the frame boxed down two
  octaves, 6 levels, streak 332 texels, 80 px tiles, the look stage two octaves down) against
  **17.7 ms for the chain before ADR-917** at the same 240 px radius (6 levels at full size, 83
  texels, 20 px tiles). Motion blur is most of either: 11.6 ms with 80 px tiles, 13.8 ms with 20 px.
  With the single-pass tile maximum at 80 px the motion blur alone took 47.8 ms.
- **Tests re-baselined** (a test that renders a small frame with bloom and asserts on it now meets
  a reference of 720 lines):
  - `test_image_look_gpu.cpp`, "post/bloom/levels reaches the pyramid from a project": at 192x128
    the chain folds the levels a 128-line frame is too small for (six=4, two=1). Its reference is
    pinned to its 128 lines, where six and two levels are what the test asks about.
  - `test_post_artifact_forensics_gpu.cpp`: the comb measurements were taken at scale 1; at 288
    lines against 720 every streak is 0.4x its authored texels and the lag the test watches no
    longer describes it (0.023-0.042 against a bar of 0.02, with no isolated peaks and the
    elongation intact). `glowmerePost()` pins the reference to 288.
  - `test_particle_weather_gpu.cpp`: a lit-pixel count saw the default bloom's halo, planned 2.5
    octaves tighter at 128 lines (3882 wrapped against 2908 free, where the test asks for 2x). The
    fixture pins its 128 lines.
  - Every other GPU test passed unchanged in the first full run on this branch (542 cases).
