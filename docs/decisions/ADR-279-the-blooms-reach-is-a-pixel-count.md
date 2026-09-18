# ADR-279: The bloom's reach is a pixel count, so supersampling halves it — measured, and not changed

**Status:** Accepted (as a finding; no code change)
**Date:** 2026-09-18

From the HDR / Exposure / Bloom Lab (ADR-277, `docs/hdr-lab/README.md` §5.2). This ADR exists
because the decision was *not to act*, and a decision not to act is worth as much as the other kind
only if the measurement behind it is written down.

## The mechanism

`PostProcessor::run` builds `clamp(bloomLevels, 1, 8)` pyramid levels starting at half resolution.
The level count is a **constant** — 6 by default, authored per scene, the same at every frame size.
So the coarsest level's texel is 2⁶ = **64 output pixels whatever the resolution**, and the filter's
reach is therefore measured in pixels rather than in fractions of the picture.

Every other spatial radius in the same file already follows the opposite convention, documented and
in one line: `pixelScale = static_cast<float>(in.height) / 720.0f`, applied to `dofMaxRadius`,
`tiltShiftMaxRadius` and `motionBlurMaxRadius`. Bloom is the stage that does not.

## The measurement

Synthetic (`testsupport::PostBench`), one source of fixed **normalised** size — 1/36 of the frame
height — at five resolutions. The normalised radius containing 99% of `bloom/up0`'s energy:

| frame | r99 (frame heights) | r99 × height (pixels) |
|---|---|---|
| 640 × 360 | 0.33889 | 122 |
| 1280 × 720 | 0.16667 | 120 |
| 1600 × 900 | 0.13333 | 120 |
| 1920 × 1080 | 0.11111 | 120 |
| 2560 × 1440 | 0.08333 | 120 |

Flat in pixels across a fourfold range.

The first version of this probe used a **one-texel** impulse and reported a constant 20 px at all
five sizes, which says nothing: a one-texel impulse is a different physical object at every
resolution. That cost a run, and the source is sized in frame fractions because of it.

Through the renderer, which is the version that matters — `render_job.cpp` sets
`quality.renderScale = min(supersample, 2)` and the whole post chain runs at the scene target. The
lab fixture's two bright sources, 1280 × 720 output:

| source | renderScale 1 | renderScale 2 | ratio |
|---|---|---|---|
| 0.3 m (15 px at 720p) | r99 = 0.11944 | r99 = 0.06042 | **0.506** |
| 4.0 m (198 px at 720p) — **control** | r99 = 0.19028 | r99 = 0.18472 | 0.971 |

The small highlight's glow covers half as much of the delivered frame with `--supersample 2` as
without. The large one, whose halo is mostly its own width, does not move — which is what says the
two frames are the same shot and the arm measured the bloom. A second control (lab case 3) is the
flat calibrated card, which does not change at all: there is no spatial frequency there for a
resolution change to reach.

## Why nothing was changed

Glowmere's deliverable is rendered with supersampling on, so this is a real look change hidden
inside a sampling flag, and the temptation is to fix it. Three reasons not to, here:

1. **The fix changes every render at every size other than the one the look was tuned at.** Making
   the halo a fixed fraction of the frame means scaling the level count with the resolution, and
   every existing project's bloom then renders differently at 1080p than it does today. That is an
   art-direction decision, not a bug fix.
2. **This lab is second of three into `post_processor.cpp`.** The Volumetric Lab follows it into the
   same file and the same scene-pass uniforms, and a look change landing beside its work cannot be
   told apart from it afterwards. The brief for this lab said so in as many words.
3. **The candidate fix is defensible but not obviously right.** `levels + round(log2(pixelScale))`
   reuses the file's own documented 720p convention rather than inventing a constant (§28), and it
   restores parity at supersample 2 exactly. It also deepens the pyramid at 1080p by one level,
   which costs a pass and widens the glow — so it is a decision with a cost, and it should be taken
   by someone who can look at the result.

## The invariant a fix would have to satisfy

Stated here so it can be turned into a test the day someone acts:

> The normalised radius containing 99% of a fixed-normalised-size highlight's bloom halo must not
> depend on `QualitySettings::renderScale`.

With the control that gives it teeth: the same measurement on a highlight twenty times larger must
already satisfy it before the change (it does — 0.971), or the arm is measuring the render scale
rather than the bloom.
