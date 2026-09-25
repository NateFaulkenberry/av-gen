# ADR-710: The march spends its steps where the media are

**Status:** Accepted -- with a look change the owner must judge (see "What it changes that was not grain")
**Date:** 2026-09-24
**Resolves:** fog brief §31 (adaptive sampling) = tornado plan §2 item 5; `docs/design/effect-library/tornado-fog-production-pass.md` §4 step 3; ADR-562 §4's deferred redistribution; ADR-577's "§31 not built"; ADR-708's blocker.
**Implemented by:** `shaders/march_schedule.wgsl` + `src/world/march_schedule.{hpp,cpp}` (its CPU twin); `shaders/volume.wgsl` (`fs_volume`, `mediumColumnInterval`/`mediumCapInterval`/`mediumCapOf`, `mediumSelfShadow`); `src/world/medium_bound.cpp` + `MediumBound`.
**Tests:** `tests/unit/test_march_schedule.cpp`, `tests/rendering/test_march_schedule_parity_gpu.cpp`, `tests/unit/test_medium_bound.cpp` (two cases added/extended), the hidden probe `tests/rendering/test_march_grain_gpu.cpp`.

---

## Context

The march took `steps` samples over the whole ray (`maxDistance`). ADR-562 §4 computed each medium's
ray interval and used it only to skip field evaluations. So a thin medium far down a long ray got one
or two samples: the Tree of Life hero's 32 steps over 4 km are 125 m apart through a 140-220 m
funnel. ADR-708 found self-shadowing turns that into speckle at every shadow step count until the
*global* count reaches 256 (+25 ms), and ADR-566/577 named the same undersampling as the fog
primitive arms' speckle. ADR-709 was done first so no perturbation could be confused with this.

## Decision

1. **A sample schedule per pixel.** `marchSchedule` merges the media's ray intervals as a *union* and
   spends the authored step count inside it, evenly by length and never coarser than the legacy
   spacing. Outside the union: if the environment fog is on, the intervals are widened to the legacy
   grid's cell edges and the gaps are marched as whole legacy cells; if it is off, the gaps are empty
   air and are skipped. **A ray that crosses no medium is marched exactly as before** -- one segment,
   `0 + (i + jitter) * maxDistance/steps`. The loop in `fs_volume` is one flat loop walking the
   segments in order, so transmittance accumulates front to back as it always did. The field is not
   touched: `vortexFilterWidth()` (the noise band-limit, ADR-389) stays `maxDistance / steps`, so only
   where the field is sampled moved, not what it is.
2. **A tighter tornado bound, because the bound is now sampling density.** With the steps inside the
   bound, a generous bound is no longer free: it spreads the samples thinner. The hero's single
   cylinder was 810 m in radius because of its wall cloud (five top radii, `cloudWidth` 5), so a ray
   through the funnel 600 m below the cloud crossed 1.6 km of "medium". The bound is now a **column**
   (funnel and debris) plus a **cap** (the wall cloud's radius, only above `h = 1 - cloudHeight`, the
   field's own gate). Two more tightenings, both field gates the bound ignored: the debris flare
   counts only when `skirtDensity > 0` (330 m of empty flare on the hero).
3. **Union, never hull -- measured by a seam.** The first version gave the schedule the hull of
   column and cap. The spacing is the span length over `steps`, and a hull jumps by the whole gap
   between funnel and cloud the moment a ray grazes the cloud: a diagonal seam across the funnel.
   The column and the cap now go to the schedule as two intervals (eight in all), and a union's
   length varies continuously. The shadow march had the same seam through the same hull and now
   marches up to three pieces (column, cap before it, cap after it) with its own steps each; a
   zero-length piece contributes zero, so the sum cannot jump.
4. **A bound defect the tightening exposed.** `axisAt` swings the axis by `(sin a, cos b) * wobble`
   with independent phases, up to `sqrt 2 * wobble`; both bound twins added `1 * wobble`. The single
   generous cylinder hid it; the column-and-cap bound let the containment test find a wall-cloud
   sample (0.008) at 411 m against a 410 m claim. Both twins now add `1.41422 * wobble`.

## Measurements

**Grain at matched luminance** (`PROBE the march's grain at matched luminance`, 640x360, Offline
tier, t = 6). Noise is the per-pixel difference between two frames 1/240 s apart (a new jitter nonce),
less the same pair with the jitter off (the motion floor), divided by the medium's own mean
contribution in the same pixels -- so an arm that got darker cannot pass for one that got cleaner.

| arm | medium mean (linear) | grain / medium, before | grain / medium, after |
|---|---|---|---|
| hero, 32 steps | 3.54 -> **1.15** | **0.291** | **0.025** |
| hero, 32 steps, 4 shadow steps @ 0.4 | 2.48 -> 0.77 | 0.291 | 0.037 |
| hero, 128 steps | 1.35 -> 0.90 | 0.058 | 0.0006 |
| hero, 256 steps (ADR-708's "clean") | 1.05 -> 0.86 | 0.013 | 0.0004 |
| fog bank / sphere / box, 32 steps | 0.14/0.31/0.37 -> 0.13/0.26/0.31 | 0.24 / 0.31 / 0.18 | 0.000 / 0.0003 / 0.0005 |
| showcase (256), modes-d (128), jitter authored 0 | -- | 0 (no jitter) | 0 |

The hero at the shipped 32 steps now has about twice the grain of the old 256-step arm and a medium
luminance within 10% of it (1.15 against 1.05): **the shipped step count now looks like the old 256.**

**Cost** (`--bench-json`, 1920x1080, 60 frames, two interleaved rounds under `tools/gpu-lock.sh`,
`volume.march` median / GPU frame p50):

| arm | before | after |
|---|---|---|
| hero, 32 steps | 3.87, 3.87 ms / 15.2, 15.3 | 4.13, 4.39 ms / 15.6, 16.5 |
| hero, 4 shadow steps | 5.05, 5.05 ms / 16.6, 16.6 | **13.8, 14.0 ms** / 25.1, 25.3 |
| fog sphere, 32 steps | 3.54, 3.67 ms / 14.8, 15.5 | 3.93, 3.93 ms / 15.3, 15.6 |

Without shadow, +0.3-0.5 ms buys what 256 global steps bought for +25 ms. With shadow the cost rises
+9 ms, because 32 samples now land in the funnel instead of about three, and each runs a shadow march
(ADR-570's cost is per in-medium sample). Still under half the 33 ms of ADR-708's clean 256-step arm.

**Byte-identity where nothing should move.** `glowmere-valley-2` at t = 6 (environment fog, no
placed medium): identical PNG before and after. **The Detail=0 gate held**: `tornado-modes-a-structure`
with every noise control scrambled at Detail 0 hashes identical (see below for its control).

## What it changes that was not grain

The review sheets show more than grain going away, and the owner has to see this before it ships:

- **The labs' storms went from pale, striated and lit to flat mid-grey silhouettes**
  (`sheet-item2-labs.png`), and the Tree of Life **night** column behind the island mostly fades
  (`sheet-item2-night.png`). The hero's day column dims by 3x in linear light, but it was clipped white
  and still is.
- **This is the old march being wrong, not the new one.** The march integrates with a left-Riemann
  step, `T * source * dt`, which overshoots by `tau / (1 - exp(-tau))` when one step's optical depth
  `tau` is large -- 5x at `tau = 5`. The labs' 0.06/m storms at 31 m steps, and the hero at 125 m,
  were in that regime. **Controlled experiment:** the *old* march with only the per-step weight made
  exact, `(1 - exp(-tau)) / extinction` -- no sample moved -- renders the labs and showcase as the same
  flat grey as the new march (`expt-analytic.png`). Two independent routes to the converged integral
  agree; the bright striated storms were the overshoot, whose brightness is proportional to each
  sample's density. Every coefficient tuned by eye against those frames (lab densities, the hero's
  emission, ADR-708's strength recommendation, ADR-580's "six of seven read well") was tuned against
  the bias -- ADR-389's family once more.
- **The tornado's detail stack is nearly invisible in a converged opaque column.** The Detail=0 gate's
  control -- Detail 0.8 must change the frame -- moved hundreds of pixels by more than 24/765 on the
  old march and moves **none** now (largest change 16/765; 7/765 at a tenth of the density). The
  control now counts changed bytes, which is what it is for (the gate is not vacuous); the finding is
  recorded here rather than hidden by a threshold. A multiplicative, mean-1 detail integrated through
  an optically thick column mostly averages out; the old march showed it one sample at a time.

Nothing in the content was re-tuned here: tuning is the plan's item 4 and the owner's eye (ADR-441).

## Consequences

- `test_volume_temporal_gpu.cpp`'s jitter case now measures 1.24e-6 (from 1.4e-5); its thresholds
  moved down with the measurement, the claim unchanged.
- The labs and night deliverable need a re-tune (lower density or more scattering/emission), and
  ADR-708's strength recommendation should be re-read on converged frames. Recommended next, not done:
  the exact per-step weight above, which makes the march nearly step-count independent for thick
  media. It would move every volumetric frame slightly (Glowmere's env fog is thin, so little), which
  is why it is its own change.
- A bound is no longer free to be generous (`medium_bound.cpp`'s header now says so). The fog bank's
  bound, `hTop` up to 40 thicknesses, is the next candidate if a thin bank shows grain.
- `volumeSteps` now means samples inside the media on a medium ray. A scene authored at 256 marches
  256 inside a 100 m storm; nothing broke, but a lab's high count now buys much less than it did.

## Rejected

- **Raising the global step count** (ADR-708's arm): +25 ms for what this does for +0.4.
- **Hull intervals** (the first version): the seam.
- **A finer noise band-limit to match the finer spacing**: a density change in the same edit,
  which is the attribution trap the plan's §4 ordering exists to avoid.
